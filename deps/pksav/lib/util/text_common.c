/*
 * Copyright (c) 2016-2017 Nicholas Corgan (n.corgan@gmail.com)
 *
 * Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
 * or copy at http://opensource.org/licenses/MIT)
 */

#include "util/text_common.h"

#include <locale.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#if defined(PKSAV_PLATFORM_MINGW) || defined(PKSAV_PLATFORM_WIN32)

#include <windows.h>
#include <stdlib.h>

/*
 * The Win32 converters fail outright (and write a partial, unterminated
 * result) when the output doesn't fit the input plus its NUL, e.g. for a
 * name that fills its field. Convert into a buffer of the needed size, then
 * copy at most num_chars units, like the C library's mbstowcs/wcstombs.
 */
void pksav_mbstowcs(
    wchar_t* p_output,
    const char* p_input,
    size_t num_chars
)
{
    int needed = MultiByteToWideChar(CP_UTF8, 0, p_input, -1, NULL, 0);
    if(needed <= 0)
    {
        return;
    }
    wchar_t* p_full = calloc((size_t)needed, sizeof(wchar_t));
    if(!p_full)
    {
        return;
    }
    MultiByteToWideChar(CP_UTF8, 0, p_input, -1, p_full, needed);
    size_t len = wcslen(p_full);
    size_t n = (len < num_chars) ? len + 1 : num_chars;
    memcpy(p_output, p_full, n * sizeof(wchar_t));
    free(p_full);
}

void pksav_wcstombs(
    char* p_output,
    const wchar_t* p_input,
    size_t num_chars
)
{
    int needed = WideCharToMultiByte(CP_UTF8, 0, p_input, -1, NULL, 0, NULL, NULL);
    if(needed <= 0)
    {
        return;
    }
    char* p_full = calloc((size_t)needed, 1);
    if(!p_full)
    {
        return;
    }
    WideCharToMultiByte(CP_UTF8, 0, p_input, -1, p_full, needed, NULL, NULL);
    size_t len = strlen(p_full);
    if(len < num_chars)
    {
        memcpy(p_output, p_full, len + 1);
    }
    else
    {
        // Don't split a UTF-8 sequence: back up to a lead byte.
        size_t n = num_chars;
        while(n > 0 && ((unsigned char)p_full[n] & 0xC0) == 0x80)
        {
            --n;
        }
        memcpy(p_output, p_full, n);
    }
    free(p_full);
}

#else

void pksav_mbstowcs(
    wchar_t* p_output,
    const char* p_input,
    size_t num_chars
)
{
    char* p_old_locale = setlocale(LC_CTYPE, NULL);
    setlocale(LC_CTYPE, "en_US.utf8");
    mbstowcs(p_output, p_input, num_chars);
    if(p_old_locale)
    {
        (void)setlocale(LC_CTYPE, p_old_locale);
    }
}

void pksav_wcstombs(
    char* p_output,
    const wchar_t* p_input,
    size_t num_chars
)
{
    char* p_old_locale = setlocale(LC_CTYPE, NULL);
    setlocale(LC_CTYPE, "en_US.utf8");
    wcstombs(p_output, p_input, num_chars);
    if(p_old_locale)
    {
        (void)setlocale(LC_CTYPE, p_old_locale);
    }
}

#endif

// C equivalent of std::distance
ssize_t wchar_map_index(
    const wchar_t* p_char_map,
    size_t char_map_size,
    wchar_t to_find
)
{
    for(size_t char_index = 0;
        char_index < char_map_size;
        ++char_index)
    {
        if(p_char_map[char_index] == to_find)
        {
            return char_index;
        }
    }
    return -1;
}

