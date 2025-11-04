// project.c ... project scan functions
// part of Multi-attribute Linear-hashed Files
// Manage creating and using Projection objects
// Last modified by Xiangjun Zai, Mar 2025

#include <stdbool.h>
#include "defs.h"
#include "project.h"
#include "reln.h"
#include "tuple.h"
#include "util.h"

// A suggestion ... you can change however you like

struct ProjectionRep {
	Reln    rel;       // need to remember Relation info
	//TODO
    char    *attrstr;       // store the attrstr from input
    Count   nattrs;         // store the number of attrs from input
    int     *attrsOrder;    // store the order and attr number from attrstr
};

// take a string of 1-based attribute indexes (e.g. "1,3,4")
// set up a ProjectionRep object for the Projection
Projection startProjection(Reln r, char *attrstr)
{
    Projection new = malloc(sizeof(struct ProjectionRep));
    assert(new != NULL);

    //TODO
    new->rel = r;
    new->attrstr = copyString(attrstr);
    // if attrstr first char is *, means select all
    if (new->attrstr[0] == '*') {
        new->nattrs = 0;
    } else {
        new->nattrs = 1;
    }
    // count number of ',' in attrstr to find number of nattrs
    for (int i = 0; i < strlen(new->attrstr); i++) {
        if (new->attrstr[i] == ',') {
            new->nattrs++;
        }
    }
    // store the attrs number in array
    if (new->nattrs != 0) {
        new->attrsOrder = malloc(new->nattrs * sizeof(int));
        char *token = strtok(attrstr, ",");
        int idx = 0;
        while (token != NULL) {
            new->attrsOrder[idx] = atoi(token);
            idx++;
            token = strtok(NULL, ",");
        }
    }

    return new;
}

void projectTuple(Projection p, Tuple t, char *buf)
{
    // TODO: Implement projection of tuple 't' according to 'p' and store result in 'buf'
	Count na = nattrs(p->rel);
	char **tupArray = malloc(na*sizeof(char *));
	tupleVals(t, tupArray);

    // return the whole tuple if attrstr is '*'
    // else, form the result from attrsOrder
    if (p->nattrs == 0) {
        tupleString(t, buf);
    }
    else {
        int result_size = p->nattrs-1;
        for (int i = 0; i < p->nattrs; i++) {
            int attr_idx = p->attrsOrder[i]-1;
            result_size += strlen(tupArray[attr_idx]);
        }
        char result[result_size];
        memset(result, 0, result_size);
        for (int i = 0; i < p->nattrs; i++) {
            int attr_idx = p->attrsOrder[i]-1;
            strcat(result, tupArray[attr_idx]);
            if (i < p->nattrs - 1) {
                strcat(result, ",");
            }
        }
        tupleString(result, buf);

    }
    freeVals(tupArray,na);
}

void closeProjection(Projection p)
{
    // TODO
    if (p->attrstr != NULL) free(p->attrstr);
    if (p->attrsOrder != NULL) free(p->attrsOrder);
    if (p != NULL) free(p);
}
