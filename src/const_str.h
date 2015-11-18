#ifndef CONST_STR_H
#define CONST_STR_H

#define _CRT_SECURE_NO_WARNINGS

#define CONST_FROM_CHAR(T, LIST)                        \
static const T T##_from_str(char* src)                  \
{                                                       \
    int i;                                              \
    for (i = 0; LIST[i]; i += 2)                        \
        if (!_stricmp(src, (char*)LIST[i + 1]))         \
            return (T)(int)LIST[i];                     \
    return (T)0;                                        \
};

#define CONST_TO_CHAR(T, LIST)                          \
static const char* T##_to_str(T src, char* def = NULL)  \
{                                                       \
    int i;                                              \
    for (i = 0; LIST[i]; i += 2)                        \
        if(src == (T)(int)LIST[i])                      \
            return (char*)LIST[i + 1];                  \
    return def;                                         \
};

#endif
