#ifndef UTILS_HPP
#define UTILS_HPP

#include <algorithm>
#include <locale>
#include <string>

static inline void ltrim(std::string &s, unsigned char ach) {
    s.erase(s.begin(),
            std::find_if(s.begin(), s.end(), [ach](unsigned char ch) {
                return !std::isspace(ch) && ch != ach;
            }));
}

static inline void rtrim(std::string &s, unsigned char ach) {
    s.erase(std::find_if(s.rbegin(), s.rend(),
                         [ach](unsigned char ch) {
                             return !std::isspace(ch) && ch != ach;
                         })
                .base(),
            s.end());
}

static inline std::string &trim(std::string &s, unsigned char ach = '\0') {
    ltrim(s, ach);
    rtrim(s, ach);
    return s;
}

static inline std::string ltrimmed(std::string s, unsigned char ach = '\0') {
    ltrim(s, ach);
    return s;
}

static inline std::string rtrimmed(std::string s, unsigned char ach = '\0') {
    rtrim(s, ach);
    return s;
}

static inline std::string trimmed(std::string s, unsigned char ach = '\0') {
    return trim(s, ach);
}

#endif // UTILS_HPP
