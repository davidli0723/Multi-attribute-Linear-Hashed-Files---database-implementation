// tuple.c ... functions on tuples
// part of Multi-attribute Linear-hashed Files
// Credt: John Shepherd
// Last modified by David LI, Apr 2025

#include "defs.h"
#include "tuple.h"
#include "reln.h"
#include "hash.h"
#include "chvec.h"
#include "bits.h"
#include "util.h"
#include "regex.h"

// return number of bytes/chars in a tuple

int tupLength(Tuple t)
{
	return strlen(t);
}

// reads/parses next tuple in input

Tuple readTuple(Reln r, FILE *in)
{
	char line[MAXTUPLEN];
	if (fgets(line, MAXTUPLEN-1, in) == NULL)
		return NULL;
	line[strlen(line)-1] = '\0';
	// count fields
	// cheap'n'nasty parsing
	char *c; int nf = 1;
	for (c = line; *c != '\0'; c++)
		if (*c == ',') nf++;
	// invalid tuple
	if (nf != nattrs(r)) return NULL;
	return copyString(line); // needs to be free'd sometime
}

// extract values into an array of strings

void tupleVals(Tuple t, char **vals)
{
	char *c = t, *c0 = t;
	int i = 0;
	for (;;) {
		while (*c != ',' && *c != '\0') c++;
		if (*c == '\0') {
			// end of tuple; add last field to vals
			vals[i++] = copyString(c0);
			break;
		}
		else {
			// end of next field; add to vals
			*c = '\0';
			vals[i++] = copyString(c0);
			*c = ',';
			c++; c0 = c;
		}
	}
}

// release memory used for separate attribute values

void freeVals(char **vals, int nattrs)
{
	int i;
    // release memory used for each attribute
	for (i = 0; i < nattrs; i++) free(vals[i]);
    // release memory used for pointer array
    free(vals);
}

// hash a tuple using the choice vector
// TODO: actually use the choice vector to make the hash

Bits tupleHash(Reln r, Tuple t)
{
	// char buf[MAXBITS+1];  //*** for debug
	Count nvals = nattrs(r);
    char **vals = malloc(nvals*sizeof(char *));
    assert(vals != NULL);
    tupleVals(t, vals);

	// hash tuple
	Bits valsHash[nvals];
	for (int i = 0; i < nvals; i++) {
		valsHash[i] = hash_any((unsigned char *)vals[i],strlen(vals[i]));
	}
	
	// form hash result with choice vector
	Bits hashResult = 0;
	ChVecItem *cv = chvec(r);
	for (int i = 0; i < MAXCHVEC; i++) {
		if (bitIsSet(valsHash[cv[i].att], cv[i].bit)) {
			hashResult = setBit(hashResult, i);
		}
	}
	// bitsString(hashResult,buf);  //*** for debug
	// printf("hash(%s) = %s\n", t, buf);  //*** for debug

    freeVals(vals,nvals);
	return hashResult;
}

// compare two tuples (allowing for "unknown" values)
// TODO: actually compare values
Bool tupleMatch(Reln r, Tuple pt, Tuple t)
{
	Count na = nattrs(r);
	char **querytupArray = malloc(na*sizeof(char *));
	tupleVals(pt, querytupArray);
	char **curtupArray = malloc(na*sizeof(char *));
	tupleVals(t, curtupArray);
	Bool match = TRUE;
	// TODO: actually compare values
	for (int i = 0; i < na; i++) {
		// if unknown value is ?, match always equal to True
		if (strchr(querytupArray[i], '?')) {
			continue;
		}
		// if unknown value has %, check by regular expression
		else if (strchr(querytupArray[i], '%')) {
			size_t pattern_size = strlen(querytupArray[i]);
			int carat = (querytupArray[i][0] != '%');
			int dollar = (querytupArray[i][strlen(querytupArray[i])-1] != '%');
			int percent_count = 0;
			for (int char_idx = 0; char_idx < pattern_size; char_idx++) {
				if (querytupArray[i][char_idx] == '%') {
					percent_count++;
				}
			}
			pattern_size += percent_count + carat + dollar;
			regex_t regex;
			int result;
			char pattern[pattern_size];
			int pattern_idx = 0;
			if (carat) {
				pattern[pattern_idx] = '^';
				pattern_idx++;
			}
			for (int char_idx = 0; char_idx < pattern_size; char_idx++) {
				if (querytupArray[i][char_idx] == '%') {
					pattern[pattern_idx++] = '.';
					pattern[pattern_idx] = '*';
				}
				else {
					pattern[pattern_idx] = querytupArray[i][char_idx];
				}
				pattern_idx++;
			}
			if (dollar) {
				pattern[pattern_idx] = '$';
			}
			
			// printf("pattern is %s, cur tuple is %s\n", pattern, curtupArray[i]);
			result = regcomp(&regex, pattern, REG_EXTENDED);
			if (result) {
				return -1;
			}

			result = regexec(&regex, curtupArray[i], 0, NULL, 0);
			if (result == REG_NOMATCH) {
				match = FALSE;
			} 
			regfree(&regex);
		}
		// if it is known value, strcmp
		else {
			if (strcmp(querytupArray[i], curtupArray[i])) {
				match = FALSE;
			}
		}
	}
	freeVals(querytupArray,na); freeVals(curtupArray,na);
	return match;
}

// puts printable version of tuple in user-supplied buffer

void tupleString(Tuple t, char *buf)
{
	strcpy(buf,t);
}

// release memory used for tuple
void freeTuple(Tuple t)
{
    if (t != NULL) {
        free(t);
    }
}
