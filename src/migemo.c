/* vim:set ts=8 sts=4 sw=4 tw=0: */
/*
 * migemo.c -
 *
 * Written By:  Muraoka Taro <koron@tka.att.ne.jp>
 * Last Change: 12-Nov-2002.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "wordbuf.h"
#include "wordlist.h"
#include "mnode.h"
#include "rxgen.h"
#include "romaji.h"
#include "filename.h"
#include "migemo.h"

#define DICT_MIGEMO "migemo-dict"
#define DICT_ROMA2HIRA "roma2hira.dat"
#define DICT_HIRA2KATA "hira2kata.dat"
#define DICT_HAN2ZEN "han2zen.dat"

#ifdef __BORLANDC__
# define EXPORTS __declspec(dllexport)
#else
# define EXPORTS
#endif

/* migemoオブジェクト */
struct _migemo
{
    int enable;
    mtree_p mtree;
    romaji* roma2hira;
    romaji* hira2kata;
    romaji* han2zen;
    rxgen* rx;
};

/*
 * migemo interfaces
 */

/*
 * 既存のmigemoオブジェクトに辞書ファイルを追加読込する。
 */
    EXPORTS
    int 
migemo_load(migemo* obj, int dict_id, char* dict_file)
{
    FILE *fp;

    if (!obj && dict_file)
	return MIGEMO_DICTID_INVALID;

    switch (dict_id)
    {
	case MIGEMO_DICTID_MIGEMO:
	    /* migemo辞書読み込み */
	    if ((fp = fopen(dict_file, "rt")) != NULL)
	    {
		mtree_p mtree = mnode_load(obj->mtree, fp);

		fclose(fp);
		if (mtree)
		{
		    obj->mtree = mtree;
		    obj->enable = 1;
		}
		else
		    return MIGEMO_DICTID_INVALID; /* Exhausted memory */
	    }
	    else
		return MIGEMO_DICTID_INVALID; /* Can't find file */
	    break;

	case MIGEMO_DICTID_ROMA2HIRA:
	    /* ローマ字辞書読み込み */
	    romaji_load(obj->roma2hira, dict_file);
	    break;

	case MIGEMO_DICTID_HIRA2KATA:
	    /* カタカナ辞書読み込み */
	    romaji_load(obj->hira2kata, dict_file);
	    break;

	case MIGEMO_DICTID_HAN2ZEN:
	    /* 半角→全角辞書読み込み */
	    romaji_load(obj->han2zen, dict_file);
	    break;

	default:
	    return MIGEMO_DICTID_INVALID;
    }

    return dict_id;
}

/*
 * (dict == NULL)として辞書を読み込ませないことも可能
 */
    EXPORTS
    migemo*
migemo_open(char* dict)
{
    migemo *obj;

    /* migemoオブジェクトと各メンバを構築 */
    if (!(obj = (migemo*)malloc(sizeof(migemo))))
	return obj;
    obj->enable = 0;
    obj->mtree = mnode_open(NULL);
    obj->rx = rxgen_open();
    obj->roma2hira =	romaji_open();
    obj->hira2kata =	romaji_open();
    obj->han2zen =	romaji_open();
    if (!obj->rx || !obj->roma2hira || !obj->hira2kata || !obj->han2zen)
    {
	migemo_close(obj);
	return obj = NULL;
    }

    /* デフォルトmigemo辞書が指定されていたらローマ字とカタカナ辞書も探す */
    if (dict)
    {
#ifndef _MAX_PATH
# define _MAX_PATH 1024 /* いい加減な数値 */
#endif
	char dir[_MAX_PATH];
	char roma_dict[_MAX_PATH], kata_dict[_MAX_PATH], h2z_dict[_MAX_PATH];
	char *tmp;

	filename_directory(dir, dict);
	tmp = strlen(dir) ? dir : ".";
	strcpy(roma_dict, tmp);
	strcpy(kata_dict, tmp);
	strcpy(h2z_dict, tmp);
	strcat(roma_dict, "/" DICT_ROMA2HIRA);
	strcat(kata_dict, "/" DICT_HIRA2KATA);
	strcat(h2z_dict, "/" DICT_HAN2ZEN);

	if (migemo_load(obj, MIGEMO_DICTID_MIGEMO, dict)
		!= MIGEMO_DICTID_INVALID)
	{
	    migemo_load(obj, MIGEMO_DICTID_ROMA2HIRA, roma_dict);
	    migemo_load(obj, MIGEMO_DICTID_HIRA2KATA, kata_dict);
	    migemo_load(obj, MIGEMO_DICTID_HAN2ZEN, h2z_dict);
	}
    }
    return obj;
}

    EXPORTS
    void
migemo_close(migemo* obj)
{
    if (obj)
    {
	if (obj->han2zen)
	    romaji_close(obj->han2zen);
	if (obj->hira2kata)
	    romaji_close(obj->hira2kata);
	if (obj->roma2hira)
	    romaji_close(obj->roma2hira);
	if (obj->rx)
	    rxgen_close(obj->rx);
	if (obj->mtree)
	    mnode_close(obj->mtree);
	free(obj);
    }
}

/*
 * query version 2
 */

    EXPORTS
    static void
migemo_query_proc(mnode* p, void* data)
{
    rxgen *rx = (rxgen*)data;
    wordlist_p list = p->list;

    for (; list; list = list->next)
	rxgen_add(rx, list->ptr);
}

/*
 * バッファを用意してmnodeに再帰で書き込ませる
 */
    EXPORTS
    static void
add_mnode_query(migemo* object, unsigned char* query)
{
    mnode *pnode;

    if ((pnode = mnode_query(object->mtree, query)) != NULL)
	mnode_traverse(pnode, migemo_query_proc, object->rx);
}

    EXPORTS
    static int
add_roma(migemo* object, unsigned char* query)
{
    unsigned char *stop, *hira, *kata;

    hira = romaji_convert(object->roma2hira, query, &stop);
    if (!stop)
    {
	rxgen_add(object->rx, hira);
	/* 平仮名による辞書引き */
	add_mnode_query(object, hira);
	/* 片仮名文字列を生成し候補に加える */
	kata = romaji_convert(object->hira2kata, hira, NULL);
	rxgen_add(object->rx, kata);
	/* カタカナによる辞書引き */
	add_mnode_query(object, kata);
	romaji_release(object->hira2kata, kata); /* カタカナ解放 */
    }
    romaji_release(object->roma2hira, hira); /* 平仮名解放 */

    return stop ? 1 : 0;
}

/*
 * ローマ字変換が不完全だった時に、[aiueo]および"xn"と"xtu"を補って変換して
 * みる。
 */
    EXPORTS
    static void
add_dubious_roma(migemo* object, rxgen* rx, unsigned char* query)
{
    int len;
    char *buf;
    unsigned char candidate[] = "aiueo", *ptr;

    if (!(len = strlen(query)))
	return;
    if (!(buf = malloc(len + 1 + 3))) /* NULと拡張文字用(最長:xtu) */
	return;
    strcpy(buf, query);
    buf[len + 1] = '\0';

    if (!strchr(candidate, buf[len - 1]))
    {
	/* [aiueo]を順番に補う */
	for (ptr = candidate; *ptr; ++ptr)
	{
	    buf[len] = *ptr;
	    add_roma(object, buf);
	}
	/* 未確定単語の長さが2未満か、未確定文字の直前が拇印ならば… */
	if (len < 2 || strchr(candidate, buf[len - 2]))
	{
	    if (buf[len - 1] == 'n')
	    {
		/* 「ん」を補ってみる */
		strcpy(&buf[len - 1], "xn");
		add_roma(object, buf);
	    }
	    else
	    {
		/* 「っ」を補ってみる */
		strcpy(&buf[len - 1], "xtu");
		add_roma(object, buf);
	    }
	}
    }

    free(buf);
}

/*
 * queryを文節に分解する。文節の切れ目は通常アルファベットの大文字。文節が複
 * 数文字の大文字で始まった文節は非大文字を区切りとする。
 */
    static wordlist_p
parse_query(unsigned char* query)
{
    unsigned char *buf = query;
    wordlist_p querylist = NULL, *pp = &querylist;

    while (*buf != '\0')
    {
	unsigned char *start = buf++;

	if (isupper(start[0]) && isupper(buf[0]))
	{
	    ++buf;
	    while (buf[0] != '\0' && isupper(buf[0]))
		++buf;
	}
	else
	    while (buf[0] != '\0' && !isupper(buf[0]))
		++buf;
	*pp = wordlist_open_len(start, buf - start);
	pp = &(*pp)->next;
    }
    return querylist;
}

/*
 * 1つの単語をmigemo変換。引数のチェックは行なわない。
 */
    static unsigned char*
query_a_word(migemo* object, unsigned char* query)
{
    unsigned char* answer;
    unsigned char* zen;

    /* query自信はもちろん候補に加える */
    rxgen_add(object->rx, query);

    /* queryを全角にして候補に加える */
    zen = romaji_convert(object->han2zen, query, NULL);
    if (zen != NULL)
    {
	rxgen_add(object->rx, zen);
	romaji_release(object->han2zen, zen);
    }

    /* 平仮名、カタカナ、及びそれによる辞書引き追加 */
    if (add_roma(object, query))
	add_dubious_roma(object, object->rx, query);

    /* queryそのものによる辞書引き */
    add_mnode_query(object, query);

    /* 検索パターン(正規表現)生成 */
    answer = rxgen_generate(object->rx);
    rxgen_reset(object->rx);

    return answer;
}

    static void
query_free(unsigned char* buf)
{
    rxgen_release(NULL, buf);
}

    EXPORTS
    unsigned char*
migemo_query(migemo* object, unsigned char* query)
{
    unsigned char *retval = NULL;
    wordlist_p querylist = NULL;
    wordbuf_p outbuf = NULL;

    if (object && object->rx && query)
    {
	wordlist_p p;

	querylist = parse_query(query);
	if (querylist == NULL)
	    goto MIGEMO_QUERY_END; /* 空queryのためエラー */
	outbuf = wordbuf_open();
	if (outbuf == NULL)
	    goto MIGEMO_QUERY_END; /* 出力用のメモリ領域不足のためエラー */

	for (p = querylist; p; p = p->next)
	{
	    unsigned char* answer;

	    //printf("HERE %s\n", p->ptr);
	    answer = query_a_word(object, p->ptr);
	    wordbuf_cat(outbuf, answer);
	    query_free(answer);
	}
    }

MIGEMO_QUERY_END:
    if (outbuf)
    {
	retval = outbuf->buf;
	outbuf->buf = NULL;
	wordbuf_close(outbuf);
    }
    if (querylist)
	wordlist_close(querylist);

    return retval;
}

    EXPORTS
    void
migemo_release(migemo* p, unsigned char* string)
{
    free(string);
}

    EXPORTS
    int
migemo_set_operator(migemo* object, int index, unsigned char* op)
{
    if (object)
    {
	int retval = rxgen_set_operator(object->rx, index, op);
	return retval ? 0 : 1;
    }
    else
	return 0;
}

    EXPORTS
    const unsigned char*
migemo_get_operator(migemo* object, int index)
{
    return object ? rxgen_get_operator(object->rx, index) : NULL;
}

    EXPORTS
    void
migemo_setproc_char2int(migemo* object, MIGEMO_PROC_CHAR2INT proc)
{
    if (object)
	rxgen_setproc_char2int(object->rx, (RXGEN_PROC_CHAR2INT)proc);
}

    EXPORTS
    void
migemo_setproc_int2char(migemo* object, MIGEMO_PROC_INT2CHAR proc)
{
    if (object)
	rxgen_setproc_int2char(object->rx, (RXGEN_PROC_INT2CHAR)proc);
}

/*
 * migemo辞書が読み込まれているかを調べる
 */
    EXPORTS
    int
migemo_is_enable(migemo* obj)
{
    return obj ? obj->enable : 0;
}

#if 1
/*
 * 主にデバッグ用の隠し関数
 */
    EXPORTS
    void
migemo_print(migemo* object)
{
    if (object)
	mnode_print(object->mtree, NULL);
}
#endif
