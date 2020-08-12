#pragma once

#include "extern/iomanip.h"
#include "util.h"
#include "key_verifier.h"

struct PrettyException {
  string text;
};

struct StreamPos {
  optional<string> filename;
  int line;
  int column;
};

class PrettyInputArchive {
  public:
    PrettyInputArchive(const vector<string>& inputs, const vector<string>& filenames, KeyVerifier* v);

    string eat(const char* expected = nullptr);

    struct LoaderInfo {
      string name;
      function<void(bool)> load;
      bool optional;
    };

    struct NodeData {
      deque<LoaderInfo> loaders;
      bool inherited = false;
    };

    NodeData& getNode() {
      return nodeData.back();
    }

    void error(const string& s);

    template <typename T>
    bool readMaybe(T& elem) {
      auto b = bookmark();
      is >> elem;
      if (!is) {
        is.clear();
        seek(b);
        return false;
      }
      return true;
    }

    bool eatMaybe(const string& s);

    string peek(int cnt = 1);

    template <typename T>
    PrettyInputArchive& readText(T&& elem) {
      auto b = bookmark();
      is >> std::forward<T>(elem);
      if (!is) {
        is.clear();
        seek(b);
        error("Error reading value of type: "_s + typeid(T).name());
      }
      return *this;
    }

    long bookmark();

    template <typename T>
    void loadInherited(T& elem) {
      nextElemInherited = true;
      this->operator()(elem);
    }

    struct DefInfo {
      long position;
      vector<string> params;
    };

    map<string, DefInfo> defs;
    vector<long> callStack;

    PrettyInputArchive& operator()() {
      return *this;
    }

    template <typename T, typename... Types>
    PrettyInputArchive& operator()(T&& arg1, Types&& ... args) {
      prologue(*this, arg1);
      load(*this, arg1);
      epilogue(*this, arg1);
      return this->operator()(args...);
    }

    void seek(long p);

    void startNode();

    void endNode();

    KeyVerifier& keyVerifier;
    bool inheritingKey = false;

    using is_loading = std::true_type;

    private:
    vector<NodeData> nodeData;
    bool nextElemInherited = false;
    std::istringstream is;
    vector<StreamPos> streamPos;
    KeyVerifier dummyKeyVerifier;
};

template<typename T, typename int_<decltype(serialize(std::declval<PrettyInputArchive&>(), std::declval<T&>()))>::type = 0>
void load(PrettyInputArchive& ar1, T& obj) {
  serialize(ar1, obj);
}

template<typename T, typename int_<decltype(serialize(std::declval<PrettyInputArchive&>(), std::declval<T&>(), std::declval<unsigned>()))>::type = 0>
void load(PrettyInputArchive& ar1, T& obj) {
  serialize(ar1, obj, unsigned(1000));
}

template<typename T, typename int_<decltype(std::declval<T&>().serialize(std::declval<PrettyInputArchive&>(), std::declval<unsigned>()))>::type = 0>
void load(PrettyInputArchive& ar1, T& obj) {
  obj.serialize(ar1, unsigned(1000));
}

template<typename T, typename int_<decltype(std::declval<T&>().serialize(std::declval<PrettyInputArchive&>()))>::type = 0>
void load(PrettyInputArchive& ar1, T& obj) {
  obj.serialize(ar1);
}

template<typename T>
void load(PrettyInputArchive& ar1, cereal::base_class<T>& obj) {
  load(ar1, *obj.base_ptr);
}

namespace variant_detail {
  template<int N, class Variant, class ... Args>
  typename std::enable_if<N == Variant::num_types>::type
  load_variant(PrettyInputArchive & ar, const string& target, Variant & /*variant*/) {
    ar.error("Element \"" + target + "\" not part of type " + Variant::getVariantName());
  }

  template<int N, class Variant, class H, class ... T>
  typename std::enable_if<N < Variant::num_types, void>::type
  load_variant(PrettyInputArchive & ar1, const string& target, Variant & variant) {
    if (variant.getName(N) == target) {
      H value;
      ar1(value);
      variant = Variant(value);
    } else
      load_variant<N+1, Variant, T...>(ar1, target, variant);
  }
}

//! Loading for NamedVariant
template <typename VariantType1, const char* Str(bool), typename... VariantTypes> inline
void serialize( PrettyInputArchive & ar, NamedVariant<Str, VariantType1, VariantTypes...> & v )
{
  string name;
  ar.readText(name);
  variant_detail::load_variant<0, NamedVariant<Str, VariantType1, VariantTypes...>, VariantType1, VariantTypes...>(
      ar, name, v);
}

//! Loading for enum types
template <class T> inline
typename std::enable_if<std::is_enum<T>::value, void>::type
serialize( PrettyInputArchive & ar, T & t)
{
  string s;
  ar.readText(s);
  if (auto res = EnumInfo<T>::fromStringSafe(s))
    t = *res;
  else
    ar.error("Error reading "_s + EnumInfo<T>::getName() + " value \"" + s + "\"");
}

template<class T> inline
typename std::enable_if<std::is_arithmetic<T>::value && !std::is_enum<T>::value, void>::type
serialize(PrettyInputArchive& ar, T& t) {
  ar.readText(t);
}

void serialize(PrettyInputArchive& ar, std::string& t);
void serialize(PrettyInputArchive& ar, char& c);
void serialize(PrettyInputArchive& ar, bool& c);

struct PrettyFlag {
  bool value = false;
};

void serialize(PrettyInputArchive& ar, PrettyFlag& c);

template <typename T>
inline void serialize(PrettyInputArchive& ar1, vector<T>& v) {
  if (!ar1.eatMaybe("append"))
    v.clear();
  string s;
  ar1.readText(s);
  if (s != "{")
    ar1.error("Expected list of items surrounded by { and }");
  while (ar1.peek() != "}") {
    T t;
    ar1(t);
    v.push_back(std::move(t));
  }
  ar1.eat("}");
}

template <typename T, typename U>
inline void serialize(PrettyInputArchive& ar1, map<T, U>& m) {
  map<T, vector<long>> bookmarks;
  auto getBookmarkFor = [&bookmarks] (const T& key, long location) -> optional<long> {
    auto& all = bookmarks[key];
    for (int i : All(all))
      if (all[i] == location) {
        if (i == 0)
          return none;
        return all[i - 1];
      }
    if (!all.empty())
      return all.back();
    return none;
  };
  set<T> keys;
  if (!ar1.eatMaybe("append"))
    m.clear();
  string s;
  ar1.readText(s);
  if (s != "{")
    ar1.error("Expected list of items surrounded by { and }");
  while (1) {
    if (ar1.peek() == "}")
      break;
    T key;
    ar1(key);
    auto thisKeyBookmark = ar1.bookmark();
    auto searchedBookmark = thisKeyBookmark;
    auto searchedKey = key;
    if (ar1.peek() != "modify" && keys.count(key))
      ar1.error("Duplicate key");
    keys.insert(key);
    U value;
    vector<long> toRead;
    while (true) {
      bool modifying = false;
      if (ar1.eatMaybe("inherit") || (modifying = ar1.eatMaybe("modify"))) {
        T inheritKey;
        ar1.inheritingKey = true;
        if (!modifying)
          ar1(inheritKey);
        else
          inheritKey = searchedKey;
        ar1.inheritingKey = false;
        toRead.push_back(ar1.bookmark());
        if (auto bookmark = getBookmarkFor(inheritKey, searchedBookmark)) {
          searchedBookmark = *bookmark;
          searchedKey = inheritKey;
          ar1.seek(*bookmark);
        } else
          ar1.error(modifying ? "Key to modify not found" : "Key to inherit not found");
      } else {
        toRead.push_back(ar1.bookmark());
        break;
      }
    }
    for (int i : All(toRead).reverse()) {
      ar1.seek(toRead[i]);
      if (i == toRead.size() - 1)
        ar1(value);
      else
        ar1.loadInherited(value);
    }
    bookmarks[key].push_back(thisKeyBookmark);
    m.erase(key);
    m.insert(make_pair(std::move(key), std::move(value)));
  }
  ar1.eat("}");
}

template <typename T, typename U>
inline void serialize(PrettyInputArchive& ar1, EnumMap<T, U>& m) {
  if (!ar1.eatMaybe("append"))
    m.clear();
  vector<pair<T, U>> v;
  ar1(v);
  EnumSet<T> used;
  for (auto& elem : v) {
    if (used.contains(elem.first))
      ar1.error("Repeated enum element: \"" + EnumInfo<T>::getString(elem.first));
    used.insert(elem.first);
    m[elem.first] = std::move(elem.second);
  }
}

template <typename T>
inline void serialize(PrettyInputArchive& ar1, optional<T>& v) {
  if (ar1.eatMaybe("append")) {
    if (!v)
      ar1.error("Appending to an optional value that was not initialized");
    auto& t = *v;
    ar1.loadInherited(t);
  } else {
    v.reset();
    if (ar1.eatMaybe("none"))
      return;
    T t;
    ar1(t);
    v = std::move(t);
  }
}

template <typename T>
class optional_no_none : public optional<T> {
  public:
  using optional<T>::optional;
};


template <typename T>
inline void serialize(PrettyInputArchive& ar1, optional_no_none<T>& v) {
  v.reset();
  if (ar1.eatMaybe("none"))
    ar1.error("This value can't be reset");
  T t;
  ar1(t);
  v = std::move(t);
}

template <typename T>
inline void serialize(PrettyInputArchive& ar1, unique_ptr<T>& v) {
  v.reset();
  if (ar1.eatMaybe("none"))
    return;
  T* t = new T();
  ar1(*t);
  v.reset(t);
}

struct EndPrettyInput {
};

inline EndPrettyInput& endInput() {
  static EndPrettyInput ret;
  return ret;
}

template <class T>
inline void handleNamePair(PrettyInputArchive& ar1, const string& name, T& value, bool optional) {
  ar1.getNode().loaders.push_back(PrettyInputArchive::LoaderInfo{name,
      [&ar1, &value](bool init){ if (init) value = T{}; ar1(value); }, optional});
}

template <class T>
inline void serialize(PrettyInputArchive& ar1, OptionalNameValuePair<T>& t) {
  handleNamePair(ar1, t.name, t.value, true);
}

template <class T>
inline void serialize(PrettyInputArchive& ar1, SkipPrettyValue<T>& t) {
}

template <class T>
inline void serialize(PrettyInputArchive& ar1, cereal::NameValuePair<T>& t) {
  handleNamePair(ar1, t.name, t.value, false);
}

template <class T>
inline void serialize(PrettyInputArchive& ar1, cereal::NameValuePair<optional<T>&>& t) {
  handleNamePair(ar1, t.name, t.value, true);
}

template <class T>
inline void serialize(PrettyInputArchive& ar1, cereal::NameValuePair<heap_optional<T>&>& t) {
  handleNamePair(ar1, t.name, t.value, true);
}

template <class T>
inline void serialize(PrettyInputArchive& ar1, cereal::NameValuePair<optional_no_none<T>&>& t) {
  handleNamePair(ar1, t.name, t.value, true);
}

template <class T, class U> inline
void serialize(PrettyInputArchive& ar1, std::pair<T, U>& t) {
  ar1(t.first, t.second);
}

namespace pretty_tuple_detail {
    template <size_t Height>
    struct serialize {
      template <class Archive, class ... Types> inline
      static void apply(Archive& ar1, std::tuple<Types...>& tuple) {
        serialize<Height - 1>::template apply(ar1, tuple);
        ar1(std::get<Height - 1>(tuple));
      }
    };

    template <>
    struct serialize<0> {
      template <class Archive, class ... Types> inline
      static void apply( Archive &, std::tuple<Types...>& ) { }
    };
  }

template <class ... Types> inline
void serialize(PrettyInputArchive& ar, std::tuple<Types...>& tuple) {
  pretty_tuple_detail::serialize<std::tuple_size<std::tuple<Types...>>::value>::template apply( ar, tuple );
}

template <class T>
typename std::enable_if<std::is_arithmetic<T>::value, void>::type
prologue(PrettyInputArchive&, T const & ) {
}

template <class T>
typename std::enable_if<!std::is_arithmetic<T>::value, void>::type
prologue(PrettyInputArchive& ar1, T const & ) {
  ar1.startNode();
}

void prettyEpilogue(PrettyInputArchive& ar1);

void serialize(PrettyInputArchive& ar1, EndPrettyInput&);

template <class T>
typename std::enable_if<std::is_arithmetic<T>::value, void>::type
epilogue(PrettyInputArchive& ar1, T const &) {
}

template <class T>
typename std::enable_if<!std::is_arithmetic<T>::value, void>::type
epilogue(PrettyInputArchive& ar1, T const &) {
  prettyEpilogue(ar1);
  ar1.endNode();
}


// Ignore these inputs
template <class T> inline
void prologue(PrettyInputArchive&, cereal::NameValuePair<T> const & ) { }

template <class T> inline
void epilogue(PrettyInputArchive&, cereal::NameValuePair<T> const & ) { }

template <class T> inline
void prologue(PrettyInputArchive&, OptionalNameValuePair<T> const & ) { }

template <class T> inline
void epilogue(PrettyInputArchive&, OptionalNameValuePair<T> const & ) { }

template <class T> inline
void prologue(PrettyInputArchive&, SkipPrettyValue<T> const & ) { }

template <class T> inline
void epilogue(PrettyInputArchive&, SkipPrettyValue<T> const & ) { }

template <class T> inline
void prologue(PrettyInputArchive&, cereal::SizeTag<T> const & ) { }

template <class T> inline
void epilogue(PrettyInputArchive&, cereal::SizeTag<T> const & ) { }

inline void prologue(PrettyInputArchive&, EndPrettyInput const & ) { }

inline void epilogue(PrettyInputArchive&, EndPrettyInput const & ) { }

template <class T> inline
void serialize(PrettyInputArchive& ar1, cereal::SizeTag<T> & t) {
  ar1(t.size);
}

