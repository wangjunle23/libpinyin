/* 
 *  libpinyin
 *  Library to deal with pinyin.
 *  
 *  Copyright (C) 2010 Peng Wu
 *  
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 * 
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *  
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <stdio.h>
#include <string.h>
#include <locale.h>
#include <glib.h>
#include "novel_types.h"
#include "phrase_large_table.h"

/* graph shortest path sentence segment. */

/* Note:
 * Currently libpinyin only supports ucs2 characters, as this is a
 * pre-processor tool for raw corpus, it will skip all sentences
 * which contains non-ucs2 characters.
 */

static PhraseLargeTable * g_phrases = NULL;

struct SegmentStep{
    phrase_token_t m_handle;
    utf16_t * m_phrase;
    size_t m_phrase_len;
    //use formula W = number of words. Zero handle means one word.
    size_t m_nword;
    //backtrace information, -1 one step backward.
    gint m_backward_nstep;
public:
    SegmentStep(){
        m_handle = 0;
        m_phrase = NULL;
        m_phrase_len = 0;
        m_nword = UINT_MAX;
        m_backward_nstep = -0;
    }
};

bool backtrace(GArray * steps, glong phrase_len, GArray * strings);

//Note: do not free phrase, as it is used by strings (array of segment).
bool segment(PhraseLargeTable * phrases, //Lookup Phrase
             utf16_t * phrase,
             glong phrase_len,
             GArray * strings /* Array of Segment *. */){
    /* Prepare for shortest path segment dynamic programming. */
    GArray * steps = g_array_new(TRUE, TRUE, sizeof(SegmentStep));
    SegmentStep step;
    for ( glong i = 0; i < phrase_len + 1; ++i ){
        g_array_append_val(steps, step);
    }

    for ( glong i = 0; i < phrase_len + 1; ++i ) {
        SegmentStep * step_begin = &g_array_index(steps, SegmentStep, i);
        size_t nword = step_begin->m_nword;
        for ( glong k = i + 1; k < phrase_len + 1; ++k ) {
            size_t len = k - i;
            utf16_t * cur_phrase = phrase + i;

            phrase_token_t token = 0;
            int result = g_phrases->search(len, cur_phrase, token);
            if ( !(result & SEARCH_OK) ){
                token = 0;
                if ( 1 != len )
                    continue;
            }
            ++nword;
            SegmentStep * step_end = &g_array_index(steps, SegmentStep, k);
            if ( nword < step_end->m_nword ) {
                step_end->m_handle = token;
                step_end->m_phrase = cur_phrase;
                step_end->m_phrase_len = len;
                step_end->m_nword = nword;
                step_end->m_backward_nstep = i - k;
            }
            if ( !(result & SEARCH_CONTINUED) )
                break;
        }
    }
    return backtrace(steps, phrase_len, strings);
}

bool backtrace(GArray * steps, glong phrase_len, GArray * strings){
    //backtracing to get the result.
    size_t cur_step = phrase_len;
    g_array_set_size(strings, 0);
    while ( cur_step ){
        SegmentStep * step = &g_array_index(steps, SegmentStep, cur_step);
        step->m_nword = 0; step->m_backward_nstep = 0;
        g_array_append_val(strings, *step);
        cur_step = cur_step + step->m_backward_nstep;
    }

    //reverse the strings
    for ( size_t i = 0; i < strings->len / 2; ++i ) {
        SegmentStep * head, * tail;
        head = &g_array_index(strings, SegmentStep, i);
        tail = &g_array_index(strings, SegmentStep, strings->len - 1 - i );
        SegmentStep tmp;
        tmp = *head;
        *head = *tail;
        *tail = tmp;
    }

    g_array_free(steps, TRUE);
    return true;
}

void print_help(){
    printf("Usage: mmseg [--generate-extra-enter]\n");
    exit(1);
}

int main(int argc, char * argv[]){
    int i = 1;
    bool gen_extra_enter = false;

    setlocale(LC_ALL, "");
    //deal with options.
    while ( i < argc ){
        if ( strcmp ("--help", argv[i]) == 0) {
            print_help();
        }else if (strcmp("--generate-extra-enter", argv[i]) == 0) {
            gen_extra_enter = true;
        }
        ++i;
    }

    //init phrase table
    g_phrases =  new PhraseLargeTable;
    FILE * gb_file = fopen("../../data/gb_char.table", "r");
    if ( gb_file == NULL ){
	fprintf(stderr, "can't open gb_char.table!\n");
	exit(1);
    }
    g_phrases->load_text(gb_file);
    fclose(gb_file);

    FILE * gbk_file = fopen("../../data/gbk_char.table", "r");
    if ( gbk_file == NULL ){
	fprintf(stderr, "can't open gbk_char.table!\n");
	exit(1);
    }
    g_phrases->load_text(gbk_file);
    fclose(gbk_file);

    MemoryChunk * chunk = new MemoryChunk;
    g_phrases->store(chunk);
    g_phrases->load(chunk);

    char * linebuf = NULL;
    size_t size = 0;
    while(getline(&linebuf, &size, stdin)) {
        if ( feof(stdin) )
            break;
        linebuf[strlen(linebuf) - 1] = '\0';

        //check non-ucs2 characters
        const glong num_of_chars = g_utf8_strlen(linebuf, -1);
        glong len = 0;
        utf16_t * sentence = g_utf8_to_utf16(linebuf, -1, NULL, &len, NULL);
        if ( len != num_of_chars ) {
            fprintf(stderr, "non-ucs2 characters encountered:%s.\n", linebuf);
            continue;
        }

        //do segment stuff
        GArray * strings = g_array_new(TRUE, TRUE, sizeof(SegmentStep));
        segment(g_phrases, sentence, len, strings);

        //print out the split phrase
        for ( glong i = 0; i < strings->len; ++i ) {
            SegmentStep * step = &g_array_index(strings, SegmentStep, i);
            char * string = g_utf16_to_utf8( step->m_phrase, step->m_phrase_len, NULL, NULL, NULL);
            printf("%s\n", string);
            g_free(string);
        }

        //print extra enter
        if ( gen_extra_enter )
            printf("\n");

        g_array_free(strings, TRUE);
        g_free(sentence);
    }
    return 0;
}
