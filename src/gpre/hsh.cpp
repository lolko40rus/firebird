//____________________________________________________________
//
//		PROGRAM:	C preprocessor
//		MODULE:		hsh.cpp
//		DESCRIPTION:	Hash table and symbol manager
//
//  The contents of this file are subject to the Interbase Public
//  License Version 1.0 (the "License"); you may not use this file
//  except in compliance with the License. You may obtain a copy
//  of the License at http://www.Inprise.com/IPL.html
//
//  Software distributed under the License is distributed on an
//  "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express
//  or implied. See the License for the specific language governing
//  rights and limitations under the License.
//
//  The Original Code was created by Inprise Corporation
//  and its predecessors. Portions created by Inprise Corporation are
//  Copyright (C) Inprise Corporation.
//
//  All Rights Reserved.
//  Contributor(s): ______________________________________.
//  TMN (Mike Nordell) 11.APR.2001 - Reduce compiler warnings
//
//
//____________________________________________________________
//
//	$Id: hsh.cpp,v 1.18 2004-01-28 07:50:27 robocop Exp $
//

#include "firebird.h"
#include "../gpre/gpre.h"
#include "../gpre/parse.h"
#include "../gpre/hsh_proto.h"
#include "../gpre/gpre_proto.h"
#include "../gpre/msc_proto.h"


static int hash(const SCHAR*);
static bool scompare(const SCHAR*, const SCHAR*);
static bool scompare2(const SCHAR*, const SCHAR*);

const int HASH_SIZE = 211;

static gpre_sym* hash_table[HASH_SIZE];
static gpre_sym* key_symbols;

static struct word {
	SCHAR *keyword;
	enum kwwords id;
}  keywords[] = {
#include "../gpre/hsh.h"
};


//____________________________________________________________
//  
//		Release space used by keywords.
//  

void HSH_fini(void)
{
	gpre_sym* symbol;

	while (key_symbols) {
		symbol = key_symbols;
		key_symbols = (gpre_sym*) key_symbols->sym_object;
		HSH_remove(symbol);
		MSC_free((UCHAR *) symbol);
	}
}


//____________________________________________________________
//  
//		Initialize the hash table.  This mostly involves
//		inserting all known keywords.
//  

void HSH_init(void)
{
	SCHAR *string;
	gpre_sym* symbol;
	gpre_sym** ptr;
	int i;
	word* a_word;

	for (ptr = hash_table, i = 0; i < HASH_SIZE; i++)
		*ptr++ = NULL;

	fflush(stdout);
	for (i = 0, a_word = keywords; i < FB_NELEM(keywords); i++, a_word++) {
		for (string = a_word->keyword; *string; string++);
		symbol = (gpre_sym*) MSC_alloc(SYM_LEN);
		symbol->sym_type = SYM_keyword;
		symbol->sym_string = a_word->keyword;
		symbol->sym_keyword = (int) a_word->id;
		HSH_insert(symbol);
		symbol->sym_object = (gpre_ctx*) key_symbols;
		key_symbols = symbol;
	}
}


//____________________________________________________________
//  
//		Insert a symbol into the hash table.
//  

void HSH_insert( gpre_sym* symbol)
{
	gpre_sym** next;
	gpre_sym* ptr;

	int h = hash(symbol->sym_string);

	for (next = &hash_table[h]; *next; next = &(*next)->sym_collision) {
		for (ptr = *next; ptr; ptr = ptr->sym_homonym)
			if (ptr == symbol)
				return;

		if (scompare(symbol->sym_string, (*next)->sym_string)) {
			/* insert in most recently seen order; 
			   This is important for alias resolution in subqueries.
			   BUT insert tokens AFTER keyword!
			   In a lookup, keyword should be found first.
			   This assumes that KEYWORDS are inserted before any other token.
			   No one should be using a keywords as an alias anyway. */

			if ((*next)->sym_type == SYM_keyword) {
				symbol->sym_homonym = (*next)->sym_homonym;
				symbol->sym_collision = NULL;
				(*next)->sym_homonym = symbol;
			}
			else {
				symbol->sym_homonym = *next;
				symbol->sym_collision = (*next)->sym_collision;
				(*next)->sym_collision = NULL;
				*next = symbol;
			}
			return;
		}
	}

	symbol->sym_collision = hash_table[h];
	hash_table[h] = symbol;
}


//____________________________________________________________
//  
//		Perform a string lookup against hash table.
//  

gpre_sym* HSH_lookup(const SCHAR* string)
{
	for (gpre_sym* symbol = hash_table[hash(string)]; symbol;
		 symbol = symbol->sym_collision)
	{
		if (scompare(string, symbol->sym_string)) 
			return symbol;
	}

	return NULL;
}

//____________________________________________________________
//  
//		Perform a string lookup against hash table.
//       Calls scompare2 which performs case insensitive
//		compare.
//  

gpre_sym* HSH_lookup2(const SCHAR* string)
{
	for (gpre_sym* symbol = hash_table[hash(string)]; symbol;
		 symbol = symbol->sym_collision)
	{
		if (scompare2(string, symbol->sym_string)) 
			return symbol;
	}

	return NULL;
}


//____________________________________________________________
//  
//		Remove a symbol from the hash table.
//  

void HSH_remove( gpre_sym* symbol)
{
	gpre_sym** next;
	gpre_sym** ptr;
	gpre_sym* homonym;

	int h = hash(symbol->sym_string);

	for (next = &hash_table[h]; *next; next = &(*next)->sym_collision)
		if (symbol == *next)
			if (homonym = symbol->sym_homonym) {
				homonym->sym_collision = symbol->sym_collision;
				*next = homonym;
				return;
			}
			else {
				*next = symbol->sym_collision;
				return;
			}
		else
			for (ptr = &(*next)->sym_homonym; *ptr; ptr = &(*ptr)->sym_homonym)
				if (symbol == *ptr) {
					*ptr = symbol->sym_homonym;
					return;
				}

	CPR_error("HSH_remove failed");
}


//____________________________________________________________
//  
//		Returns the hash function of a string.
//  

static int hash(const SCHAR* string)
{
	SCHAR c;

	SLONG value = 0;

	while (c = *string++)
		value = (value << 1) + UPPER(c);

	return ((value >= 0) ? value : -value) % HASH_SIZE;
}


//____________________________________________________________
//  
//		case sensitive Compare 
//  

static bool scompare(const SCHAR* string1,
					 const SCHAR* string2)
{

	while (*string1)
		if (*string1++ != *string2++)
			return false;

	if (*string2)
		return false;

	return true;
}

//____________________________________________________________
//  
//		Compare two strings
//  

static bool scompare2(const SCHAR* string1,
					  const SCHAR* string2)
{
	SCHAR c1, c2;

	while (c1 = *string1++)
		if (!(c2 = *string2++) || (UPPER(c1) != UPPER(c2)))
			return false;
	if (*string2)
		return false;

	return true;
}
