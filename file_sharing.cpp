#include "stdafx.h"
#include "file_sharing.h"
#include "progress_meter.h"
#include "save_file_info.h"
#include "parse_game.h"
#include "options.h"

#include <curl/curl.h>

FileSharing::FileSharing(const string& url, Options& o) : uploadUrl(url), options(o) {
}

static function<void(double)> progressFun;

static atomic<bool> cancel(false);

int progressFunction(void* ptr, double totalDown, double nowDown, double totalUp, double nowUp) {
  if (totalUp > 0)
    progressFun(nowUp / totalUp);
  if (totalDown > 0)
    progressFun(nowDown / totalDown);
  if (cancel) {
    cancel = false;
    return 1;
  } else
    return 0;
}

size_t dataFun(void *buffer, size_t size, size_t nmemb, void *userp) {
  string& buf = *((string*) userp);
  buf += string((char*) buffer, (char*) buffer + size * nmemb);
  return size * nmemb;
}

static string escapeUrl(string s) {
  replace_all(s, " ", "%20");
  return s;
}

static optional<string> curlUpload(const char* path, const char* url) {

  struct curl_httppost *formpost=NULL;
  struct curl_httppost *lastptr=NULL;

  curl_formadd(&formpost,
      &lastptr,
      CURLFORM_COPYNAME, "fileToUpload",
      CURLFORM_FILE, path,
      CURLFORM_END);

  curl_formadd(&formpost,
      &lastptr,
      CURLFORM_COPYNAME, "submit",
      CURLFORM_COPYCONTENTS, "send",
      CURLFORM_END);

  if (CURL* curl = curl_easy_init()) {
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, dataFun);
    string ret;
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ret);
    /* what URL that receives this POST */ 
    curl_easy_setopt(curl, CURLOPT_URL, escapeUrl(url).c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPPOST, formpost);
    // Internal CURL progressmeter must be disabled if we provide our own callback
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, false);
    // Install the callback function
    curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION, progressFunction);
    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK)
      ret = string("Upload failed: ") + curl_easy_strerror(res);
    curl_easy_cleanup(curl);
    curl_formfree(formpost);
    if (!ret.empty())
      return ret;
    else
      return none;
  } else
    return string("Failed to initialize libcurl");
}

optional<string> FileSharing::uploadRetired(const string& path, ProgressMeter& meter) {
  if (!options.getBoolValue(OptionId::ONLINE))
    return none;
  progressFun = [&] (double p) { meter.setProgress(p);};
  return curlUpload(path.c_str(), (uploadUrl + "/upload2.php").c_str());
}

optional<string> FileSharing::uploadSite(const string& path, ProgressMeter& meter) {
  if (!options.getBoolValue(OptionId::ONLINE))
    return none;
  progressFun = [&] (double p) { meter.setProgress(p);};
  return curlUpload(path.c_str(), (uploadUrl + "/upload_site.php").c_str());
}

void FileSharing::uploadHighscores(const string& path) {
  if (options.getBoolValue(OptionId::ONLINE)) {
    progressFun = [] (double p) {};
    curlUpload(path.c_str(), (uploadUrl + "/upload_scores2.php").c_str());
  }
}

void FileSharing::init() {
  curl_global_init(CURL_GLOBAL_ALL);
}

void FileSharing::uploadGameEvent(const map<string, string>& data) {
  if (options.getBoolValue(OptionId::ONLINE)) {
    string params;
    for (auto& elem : data) {
      if (!params.empty())
        params += "&";
      params += elem.first + "=" + elem.second;
    }
    if (CURL* curl = curl_easy_init()) {
      string ret;
      curl_easy_setopt(curl, CURLOPT_URL, escapeUrl(uploadUrl + "/game_event.php").c_str());
      curl_easy_setopt(curl, CURLOPT_POSTFIELDS, params.c_str());
      CURLcode res = curl_easy_perform(curl);
    }
  }
}

string FileSharing::downloadHighscores() {
  string ret;
  if (options.getBoolValue(OptionId::ONLINE))
    if(CURL* curl = curl_easy_init()) {
      curl_easy_setopt(curl, CURLOPT_URL, escapeUrl(uploadUrl + "/highscores2.php").c_str());
      curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, dataFun);
      curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5);
      curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ret);
      curl_easy_perform(curl);
      curl_easy_cleanup(curl);
    }
  return ret;
}

static vector<FileSharing::GameInfo> parseGames(const string& s) {
  std::stringstream iss(s);
  vector<FileSharing::GameInfo> ret;
  while (!!iss) {
    char buf[100];
    iss.getline(buf, 100);
    if (!iss)
      break;
    Debug() << "Parsing " << string(buf);
    vector<string> fields = split(buf, {','});
    if (fields.size() < 6)
      continue;
    Debug() << "Parsed " << fields;
    ret.push_back({fields[0], fields[1], fromString<int>(fields[2]), fromString<int>(fields[3]),
        fromString<int>(fields[4]), fromString<int>(fields[5])});
  }
  return ret;
}

vector<FileSharing::GameInfo> FileSharing::listGames() {
  if (CURL* curl = curl_easy_init()) {
    curl_easy_setopt(curl, CURLOPT_URL, escapeUrl(uploadUrl + "/get_games2.php").c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, dataFun);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5);
    string ret;
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ret);
    curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    return parseGames(ret);
  }
  return {};
}

static vector<FileSharing::SiteInfo> parseSites(const string& s) {
  std::stringstream iss(s);
  vector<FileSharing::SiteInfo> ret;
  while (!!iss) {
    char buf[300];
    iss.getline(buf, 300);
    if (!iss)
      break;
    Debug() << "Parsing " << string(buf);
    vector<string> fields = split(buf, {','});
    if (fields.size() < 6)
      continue;
    Debug() << "Parsed " << fields;
    ret.emplace_back();
    ret.back().fileInfo.filename = fields[0];
    ret.back().fileInfo.date = fromString<int>(fields[1]);
    ret.back().wonGames = fromString<int>(fields[2]);
    ret.back().totalGames = fromString<int>(fields[3]);
    ret.back().version = fromString<int>(fields[5]);
    ret.back().fileInfo.download = true;
    TextInput input(fields[4]);
    input.getArchive() >> ret.back().gameInfo;
  }
  return ret;
}

vector<FileSharing::SiteInfo> FileSharing::listSites() {
  if (!options.getBoolValue(OptionId::ONLINE))
    return {};
  if (CURL* curl = curl_easy_init()) {
    curl_easy_setopt(curl, CURLOPT_URL, escapeUrl(uploadUrl + "/get_sites.php").c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, dataFun);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5);
    string ret;
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ret);
    curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    return parseSites(ret);
  }
  return {};
}

void FileSharing::cancel() {
  ::cancel = true;
}

static size_t writeToFile(void *ptr, size_t size, size_t nmemb, FILE *stream) {
  return fwrite(ptr, size, nmemb, stream);
}

optional<string> FileSharing::download(const string& filename, const string& dir, ProgressMeter& meter) {
  if (!options.getBoolValue(OptionId::ONLINE))
    return string("Downloading not enabled!");
  progressFun = [&] (double p) { meter.setProgress(p);};
  if (CURL *curl = curl_easy_init()) {
    string path = dir + "/" + filename;
    Debug() << "Downloading to " << path;
    if (FILE* fp = fopen(path.c_str(), "wb")) {
      curl_easy_setopt(curl, CURLOPT_URL, escapeUrl(uploadUrl + "/uploads/" + filename).c_str());
      curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeToFile);
      curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
      // Internal CURL progressmeter must be disabled if we provide our own callback
      curl_easy_setopt(curl, CURLOPT_NOPROGRESS, false);
      // Install the callback function
      curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION, progressFunction);
      curl_easy_setopt(curl, CURLOPT_FAILONERROR, true);
      CURLcode res = curl_easy_perform(curl);
      string ret;
      if(res != CURLE_OK)
        ret = string("Upload failed: ") + curl_easy_strerror(res);
      curl_easy_cleanup(curl);
      fclose(fp);
      if (!ret.empty()) {
        remove(path.c_str());
        return ret;
      } else
        return none;
    } else
      return string("Failed to open file: " + path);
  } else
    return string("Failed to initialize libcurl");
}

