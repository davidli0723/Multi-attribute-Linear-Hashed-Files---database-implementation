// select.c ... select scan functions
// part of Multi-attribute Linear-hashed Files
// Manage creating and using Selection objects
// Credit: John Shepherd
// Last modified by David LI, Mar 2025

#include "defs.h"
#include "select.h"
#include "reln.h"
#include "tuple.h"
#include "bits.h"
#include "hash.h"

// A suggestion ... you can change however you like

struct SelectionRep {
	Reln    rel;                        // need to remember Relation info
	Bits    known;                      // the hash value from MAH
	Bits    unknown;                    // the unknown bits from MAH
	Page    curpage;                    // current page in scan
	int     is_ovflow;                  // are we in the overflow pages?
	Offset  curtupOffset;               // offset of current tuple within page
	//TODO
    Count   queryDepth;                 // depth of query in data file
    Bits    nstars;                     // total number of stars in lower bit
    Bits    totalcandidatePages;        // the number of candidate page when there is unknown bits in lower bits d
    Bits    curcandidatePage;           // current candidate page
    int     starsPosition[MAXCHVEC];    // store the unknown star position

    Tuple   queryTuple;                 // query tuple like '1024,?,?'
    Tuple   curTuple;                   // tuple in current scan
};

// take a query string (e.g. "1234,?,abc,?")
// set up a SelectionRep object for the scan

Selection startSelection(Reln r, char *q)
{
    Selection new = malloc(sizeof(struct SelectionRep));
    assert(new != NULL);

    new->rel = r;
    new->known = 0;
    new->unknown = 0;
    new->nstars = 0;
    new->queryTuple = q;
    Count nvals = nattrs(r);
    char **vals = malloc(nvals*sizeof(char *));
    assert(vals != NULL);
    tupleVals(q, vals);

    // TODO
    // char buf[MAXBITS+1];  //*** for debug
	Bits valsHash[nvals];
	for (int i = 0; i < nvals; i++) {
        if (!strchr(vals[i], '?') && !strchr(vals[i], '%')) {
    		valsHash[i] = hash_any((unsigned char *)vals[i],strlen(vals[i]));
        }
        // bitsString(valsHash[i],buf);  //*** for debug
        // printf("hash(%20s) = %s\n", vals[i], buf);  //*** for debug
	}

	// Partial algorithm:
	// form known bits from known attributes
	// form unknown bits from '?' and '%' attributes
    int starIdx = 0;
	ChVecItem *cv = chvec(r);
    for (int i = 0; i < depth(r) + 1; i++) {
        if (!strchr(vals[cv[i].att], '?') && !strchr(vals[cv[i].att], '%')) {
            if (bitIsSet(valsHash[cv[i].att], cv[i].bit)){
                new->known = setBit(new->known, i);
            }
        }
    }

	// compute PageID of first page
	//   using known bits and first "unknown" value
    Bits firstPage;
	if (depth(r) == 0)
		firstPage = 0;
	else {
		new->queryDepth = depth(r);
        firstPage = getLower(new->known, new->queryDepth);
        // get one more bit if pageId is smaller than split pointer
		if (firstPage < splitp(r)) {
            new->queryDepth++;
            firstPage = getLower(new->known, new->queryDepth);
        }
        // count number of stars and store all star position
        for (int i = 0; i < new->queryDepth; i++) {
            if (strchr(vals[cv[i].att], '?') || strchr(vals[cv[i].att], '%')) {
                new->nstars++;
                new->starsPosition[starIdx] = i;
                starIdx++;
            }
        }
	}

	// set all values in SelectionRep object
    new->curpage = getPage(dataFile(new->rel),firstPage);
    new->curtupOffset = 0;
    new->curTuple = pageData(new->curpage);

    if (new->nstars == 0) {
        new->curcandidatePage = 0;
        new->totalcandidatePages = 0;
    }
    else {
        new->curcandidatePage = 1;
        new->totalcandidatePages = 1 << new->nstars;
    }

    return new;
}

// get next tuple during a scan

Tuple getNextTuple(Selection q)
{
    // TODO
	// Partial algorithm:
    // if (more tuples in current page)
    //    get next matching tuple from current page

    Tuple matchTuple = NULL;
    while (q->curtupOffset < pageNTuples(q->curpage)) {
        if (tupleMatch(q->rel, q->queryTuple, q->curTuple)) {
            matchTuple = q->curTuple;
            q->curtupOffset++;
            q->curTuple = q->curTuple + tupLength(q->curTuple) + 1;
            return matchTuple;
        } else {
            q->curtupOffset++;
            q->curTuple = q->curTuple + tupLength(q->curTuple) + 1;
        }
    }
    // else if (current page has overflow)
    //    move to overflow page
    //    grab first matching tuple from page
    Offset ovid = pageOvflow(q->curpage);
    free(q->curpage);
    while (ovid != NO_PAGE) {
        q->curpage = getPage(ovflowFile(q->rel), ovid);
        q->curtupOffset = 0;
        q->curTuple = pageData(q->curpage);
        while (q->curtupOffset < pageNTuples(q->curpage)) {
            if (tupleMatch(q->rel, q->queryTuple, q->curTuple)) {
                matchTuple = q->curTuple;
                q->curtupOffset++;
                q->curTuple = q->curTuple + tupLength(q->curTuple) + 1;
                return matchTuple;
            } else {
                q->curtupOffset++;
                q->curTuple = q->curTuple + tupLength(q->curTuple) + 1;
            }
        }
        ovid = pageOvflow(q->curpage);
        free(q->curpage);
    }

    // else
    //    move to "next" bucket
    //    grab first matching tuple from data page
    // endif
    // if (current page has no matching tuples)
    //    go to next page (try again)
    // endif
    while (q->curcandidatePage < q->totalcandidatePages) {
        q->unknown = 0;
        for (int i = 0; i < q->nstars; i++) {
            if (bitIsSet(q->curcandidatePage, i)) {
                q->unknown = setBit(q->unknown, q->starsPosition[i]);
            }
        }

        Bits MAVal = q->known | q->unknown;
		MAVal = getLower(MAVal, q->queryDepth);
        // char buf[MAXBITS+1];  //*** for debug
        // bitsString(MAVal,buf);  //*** for debug
        if (MAVal >= npages(q->rel)) {
            break;
        }

        q->curpage = getPage(dataFile(q->rel),MAVal);
        q->curtupOffset = 0;
        q->curTuple = pageData(q->curpage);

        while (q->curtupOffset < pageNTuples(q->curpage)) {
            if (tupleMatch(q->rel, q->queryTuple, q->curTuple)) {
                matchTuple = q->curTuple;
                q->curtupOffset++;
                q->curTuple = q->curTuple + tupLength(q->curTuple) + 1;
                q->curcandidatePage++;
                return matchTuple;
            } else {
                q->curtupOffset++;
                q->curTuple = q->curTuple + tupLength(q->curTuple) + 1;
            }
        }
        
        Offset ovid = pageOvflow(q->curpage);
        free(q->curpage);
        while (ovid != NO_PAGE) {
            q->curpage = getPage(ovflowFile(q->rel), ovid);
            q->curtupOffset = 0;
            q->curTuple = pageData(q->curpage);
            while (q->curtupOffset < pageNTuples(q->curpage)) {
                if (tupleMatch(q->rel, q->queryTuple, q->curTuple)) {
                    // printf("matched found: ");
                    matchTuple = q->curTuple;
                    q->curtupOffset++;
                    q->curTuple = q->curTuple + tupLength(q->curTuple) + 1;
                    q->curcandidatePage++;
                    return matchTuple;
                } else {
                    q->curtupOffset++;
                    q->curTuple = q->curTuple + tupLength(q->curTuple) + 1;
                }
            }
            ovid = pageOvflow(q->curpage);
            free(q->curpage);
        }

        q->curcandidatePage++;
    }

    return NULL;
}

// clean up a SelectionRep object and associated data

void closeSelection(Selection q)
{
    // TODO
    if (q == NULL) return;
    free(q);
}
