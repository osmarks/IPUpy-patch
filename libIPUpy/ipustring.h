#ifndef _IPUSTRING_H
#define _IPUSTRING_H

typedef unsigned int size_t;


int strncmp (const char * str1, const char * str2, size_t num) {
    for (size_t i = 0; i < num; ++i) {
        if (str1[i] != str2[i]) return str1[i] - str2[i];
        if (str1[i] == '\0') break;
    }
    return 0;
}

size_t strspn ( const char * str1, const char * str2 ) {
    size_t n = 0;
    for(; str1[n] != '\0'; ++n) {
        for (size_t i = 0; str2[i] != '\0'; ++i) {
            if (str2[i] == str1[n]) goto matchfound;
        }
        break;
    matchfound:
        continue;
    }
    return n;
}

size_t strcspn ( const char * str1, const char * str2 ) {
    size_t n = 0;
    for(; str1[n] != '\0'; ++n) {
        for (size_t i = 0; str2[i] != '\0'; ++i) {
            if (str2[i] == str1[n]) return n;
        }
    }
    return n;
}

#endif // _IPUSTRING_H