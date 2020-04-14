#pragma once

#include "util.h"
#include "lasting_effect.h"

#define SIMPLE_PREDICATE(Name) \
  struct Name { \
    SERIALIZE_EMPTY()\
  }

struct CreaturePredicate;

namespace CreaturePredicates {
SIMPLE_PREDICATE(Enemy);
SIMPLE_PREDICATE(Automaton);

struct HatedBy {
  LastingEffect SERIAL(effect);
  SERIALIZE_ALL(effect)
};

struct Not {
  HeapAllocated<CreaturePredicate> SERIAL(pred);
  SERIALIZE_ALL(pred)
};

struct And {
  vector<CreaturePredicate> SERIAL(pred);
  SERIALIZE_ALL(pred)
};

struct Or {
  vector<CreaturePredicate> SERIAL(pred);
  SERIALIZE_ALL(pred)
};

#define CREATURE_PREDICATE_LIST\
  X(Enemy, 0)\
  X(Automaton, 1)\
  X(LastingEffect, 2)\
  X(HatedBy, 3)\
  X(Not, 4)\
  X(And, 5)\
  X(Or, 6)

#define VARIANT_NAME CreaturePredicate
#define VARIANT_TYPES_LIST CREATURE_PREDICATE_LIST

#include "gen_variant.h"

#undef VARIANT_TYPES_LIST
#undef VARIANT_NAME

template <class Archive>
void serialize(Archive& ar1, CreaturePredicate&);

}

struct CreaturePredicate : CreaturePredicates::CreaturePredicate {
  using CreaturePredicates::CreaturePredicate::CreaturePredicate;
  bool apply(const Creature* victim, const Creature* attacker) const;
  string getName(bool negated = false) const;
};