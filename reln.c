// reln.c ... functions on Relations
// part of Multi-attribute Linear-hashed Files
// Credit: John Shepherd
// Last modified by David LI, Apr 2025

#include "defs.h"
#include "reln.h"
#include "page.h"
#include "tuple.h"
#include "chvec.h"
#include "bits.h"
#include "hash.h"

#define HEADERSIZE (3*sizeof(Count)+sizeof(Offset))

struct RelnRep {
	Count  nattrs; // number of attributes
	Count  depth;  // depth of main data file
	Offset sp;     // split pointer
    Count  npages; // number of main data pages
    Count  ntups;  // total number of tuples
	Count  pagecap;// split after c insertion 
	Count  curcap; // number of insertion
	ChVec  cv;     // choice vector

	char   mode;   // open for read/write
	FILE  *info;   // handle on info file
	FILE  *data;   // handle on data file
	FILE  *ovflow; // handle on ovflow file
};

// create a new relation (three files)

Status newRelation(char *name, Count nattrs, Count npages, Count d, char *cv)
{
    char fname[MAXFILENAME];
	Reln r = malloc(sizeof(struct RelnRep));
	r->nattrs = nattrs; r->depth = d; r->sp = 0; 
	r->pagecap = 1024/(10*nattrs); 
	r->curcap = 0;
	r->npages = npages; r->ntups = 0; r->mode = 'w';
	assert(r != NULL);
	// store att and bit value into r->cv
	if (parseChVec(r, cv, r->cv) != OK) return ~OK;
	sprintf(fname,"%s.info",name);
	r->info = fopen(fname,"w");
	assert(r->info != NULL);
	sprintf(fname,"%s.data",name);
	r->data = fopen(fname,"w");
	assert(r->data != NULL);
	sprintf(fname,"%s.ovflow",name);
	r->ovflow = fopen(fname,"w");
	assert(r->ovflow != NULL);
	int i;
	for (i = 0; i < npages; i++) addPage(r->data);
	closeRelation(r);
	return 0;
}

// check whether a relation already exists

Bool existsRelation(char *name)
{
	char fname[MAXFILENAME];
	sprintf(fname,"%s.info",name);
	FILE *f = fopen(fname,"r");
	if (f == NULL)
		return FALSE;
	else {
		fclose(f);
		return TRUE;
	}
}

// set up a relation descriptor from relation name
// open files, reads information from rel.info

Reln openRelation(char *name, char *mode)
{
	Reln r;
	r = malloc(sizeof(struct RelnRep));
	assert(r != NULL);
	char fname[MAXFILENAME];
	sprintf(fname,"%s.info",name);
	r->info = fopen(fname,mode);
	assert(r->info != NULL);
	sprintf(fname,"%s.data",name);
	r->data = fopen(fname,mode);
	assert(r->data != NULL);
	sprintf(fname,"%s.ovflow",name);
	r->ovflow = fopen(fname,mode);
	assert(r->ovflow != NULL);
	// Naughty: assumes Count and Offset are the same size
	int n = fread(r, sizeof(Count), 7, r->info);
	assert(n == 7);
	n = fread(r->cv, sizeof(ChVecItem), MAXCHVEC, r->info);
	assert(n == MAXCHVEC);
	r->mode = (mode[0] == 'w' || mode[1] =='+') ? 'w' : 'r';
	return r;
}

// release files and descriptor for an open relation
// copy latest information to .info file

void closeRelation(Reln r)
{
	// make sure updated global data is put in info
	// Naughty: assumes Count and Offset are the same size
	if (r->mode == 'w') {
		fseek(r->info, 0, SEEK_SET);
		// write out core relation info (#attr,#pages,d,sp)
		int n = fwrite(r, sizeof(Count), 7, r->info);
		assert(n == 7);
		// write out choice vector
		n = fwrite(r->cv, sizeof(ChVecItem), MAXCHVEC, r->info);
		assert(n == MAXCHVEC);
	}
	fclose(r->info);
	fclose(r->data);
	fclose(r->ovflow);
	free(r);
}

// splitting function to split current page into two
void splitting(Reln r)
{
	// add one page at the end of Reln r
	r->npages++;
	addPage(dataFile(r));

	// rearrange tuple into old and new page
	// create a temp page to store old page data
	PageID oldpId = r->sp;
	Page tmpPage = getPage(r->data,oldpId);
	Tuple tmpTuple = pageData(tmpPage);
	Page oldPage = newPage();
	pageSetOvflow(oldPage, pageOvflow(getPage(r->data, oldpId)));
	putPage(r->data, oldpId, oldPage);

	Bits h, p;
	Count scannedTup = 0;

	// scan through current data page
	// the number of Tuple in current page should not require an overflow page
	// there is always enough space to store the tuple in current page
	while (scannedTup != pageNTuples(tmpPage)) {
		h = tupleHash(r,tmpTuple);
		// should always consider depth + 1 bits in splitting
		p = getLower(h, r->depth+1);
		Page pg = getPage(r->data,p);
		if (addToPage(pg,tmpTuple) == OK) {
			putPage(r->data,p,pg);
		}
		tmpTuple = tmpTuple + tupLength(tmpTuple) + 1;
		scannedTup++;
	}

	// scan through overflow page
	while (pageOvflow(tmpPage) != NO_PAGE) {
		PageID ovpId = pageOvflow(tmpPage);
		tmpPage = getPage(r->ovflow, ovpId);
		tmpTuple = pageData(tmpPage);

		oldPage = newPage();
		pageSetOvflow(oldPage, pageOvflow(getPage(r->ovflow, ovpId)));
		putPage(r->ovflow, ovpId, oldPage);

		scannedTup = 0;
		// scan through tuple
		while (scannedTup != pageNTuples(tmpPage)) {
			h = tupleHash(r,tmpTuple);
			p = getLower(h, r->depth+1);
			Page pg = getPage(r->data,p);

			// insertion to data page
			if (addToPage(pg,tmpTuple) == OK) {
				putPage(r->data,p,pg);
				scannedTup++;
				tmpTuple = tmpTuple + tupLength(tmpTuple) + 1;
				continue;
			}

			// create an overflow page if there is no space for current tuple
			// insert to overflow page afterward
			if (pageOvflow(pg) == NO_PAGE) {
				// add first overflow page in chain
				PageID newp = addPage(r->ovflow);
				pageSetOvflow(pg,newp);
				putPage(r->data,p,pg);
				Page newpg = getPage(r->ovflow,newp);
				if (addToPage(newpg,tmpTuple) == OK) {
					putPage(r->ovflow,newp,newpg);
					scannedTup++;
					tmpTuple = tmpTuple + tupLength(tmpTuple) + 1;
					continue;
				}
			}

			// find space to insert in overflow pages
			Page ovpg, prevpg = NULL;
			PageID ovp, prevp = NO_PAGE;
			ovp = pageOvflow(pg);
			free(pg);
			
			int inserted = 0;
			while (ovp != NO_PAGE) {
				ovpg = getPage(r->ovflow, ovp);
				if (addToPage(ovpg, tmpTuple) != OK) {
					if (prevpg != NULL) free(prevpg);
					prevp = ovp; prevpg = ovpg;
					ovp = pageOvflow(ovpg);
				} else {
					if (prevpg != NULL) free(prevpg);
					putPage(r->ovflow,ovp,ovpg);
					scannedTup++;
					tmpTuple = tmpTuple + tupLength(tmpTuple) + 1;
					inserted = 1;
					break;
				}
			}

			// create a new overflow page if all overflow pages are full
			// insert to the new overflow page
			if (!inserted) {
				assert(prevpg != NULL);
				// make new ovflow page
				PageID newp = addPage(r->ovflow);
				// insert tuple into new page
				Page newpg = getPage(r->ovflow,newp);
				if (addToPage(newpg,tmpTuple) == OK) {
					putPage(r->ovflow,newp,newpg);
					pageSetOvflow(prevpg,newp);
					putPage(r->ovflow,prevp,prevpg);
					scannedTup++;
					tmpTuple = tmpTuple + tupLength(tmpTuple) + 1;
					inserted = 1;
				}
			}
			
		}
	}

	// update depth and sp position
	r->sp++;
	if (r->sp == 1 << r->depth) {
		r->depth++;
		r->sp = 0;
	}
}

// insert a new tuple into a relation
// returns index of bucket where inserted
// - index always refers to a primary data page
// - the actual insertion page may be either a data page or an overflow page
// returns NO_PAGE if insert fails completely
// TODO: include splitting and file expansion
PageID addToRelation(Reln r, Tuple t)
{
	// if current insertion count reach the max insertion capacity, split page first, then insert
	if (r->curcap == r->pagecap) {
		splitting(r);
		r->curcap = 0;
	}

	r->curcap++;
	Bits h, p;
	// char buf[MAXBITS+5]; //*** for debug
	// hash tuple
	h = tupleHash(r,t);
	// find the pageId to store tuple
	// compute the lowest d bits or d + 1 in tuple's hash value depends on split pointer
	// the computed result decided which page to store
	if (r->depth == 0)
		p = 0;
	else {
		p = getLower(h, r->depth);
		if (p < r->sp) p = getLower(h, r->depth+1);
	}
	// bitsString(h,buf); printf("hash = %s\n",buf); //*** for debug
	// bitsString(p,buf); printf("page = %s\n",buf); //*** for debug
	Page pg = getPage(r->data,p);
	// insert into page if there is enough space in page
	if (addToPage(pg,t) == OK) {
		putPage(r->data,p,pg);
		r->ntups++;
		return p;
	}

	// primary data page full, looking for overflow page
	// if there is no overflow page under current page
	// create the first overflow page and add tuple into overflow page
	if (pageOvflow(pg) == NO_PAGE) {
		// add first overflow page in chain
		PageID newp = addPage(r->ovflow);
		pageSetOvflow(pg,newp);
		putPage(r->data,p,pg);
		Page newpg = getPage(r->ovflow,newp);
		// can't add to a new page; we have a problem
		if (addToPage(newpg,t) != OK) return NO_PAGE;
		putPage(r->ovflow,newp,newpg);
		r->ntups++;
		return p;
	}
	// there is overflow page, start scanning overflow page to add tuple in it
	else {
		// scan overflow chain until we find space
		// worst case: add new ovflow page at end of chain
		Page ovpg, prevpg = NULL;
		PageID ovp, prevp = NO_PAGE;
		ovp = pageOvflow(pg);
		free(pg);

		// looping all oveflow pages
		while (ovp != NO_PAGE) {
			ovpg = getPage(r->ovflow, ovp);
			// go to next overflow page if cannot add tuple
			if (addToPage(ovpg,t) != OK) {
			    if (prevpg != NULL) free(prevpg);
				prevp = ovp; prevpg = ovpg;
				ovp = pageOvflow(ovpg);
			}
			// if can add tuple into overflow page, add it
			else {
				if (prevpg != NULL) free(prevpg);
				putPage(r->ovflow,ovp,ovpg);
				r->ntups++;
				return p;
			}
		}
		// all overflow pages are full; add another to chain
		// at this point, there *must* be a prevpg
		assert(prevpg != NULL);
		// make new ovflow page
		PageID newp = addPage(r->ovflow);
		// insert tuple into new page
		Page newpg = getPage(r->ovflow,newp);
        if (addToPage(newpg,t) != OK) return NO_PAGE;
        putPage(r->ovflow,newp,newpg);
		// link to existing overflow chain
		pageSetOvflow(prevpg,newp);
		putPage(r->ovflow,prevp,prevpg);
        r->ntups++;
		return p;
	}
	return NO_PAGE;
}

// external interfaces for Reln data

FILE *dataFile(Reln r) { return r->data; }
FILE *ovflowFile(Reln r) { return r->ovflow; }
Count nattrs(Reln r) { return r->nattrs; }
Count npages(Reln r) { return r->npages; }
Count ntuples(Reln r) { return r->ntups; }
Count depth(Reln r)  { return r->depth; }
Count splitp(Reln r) { return r->sp; }
ChVecItem *chvec(Reln r)  { return r->cv; }


// displays info about open Reln

void relationStats(Reln r)
{
	printf("Global Info:\n");
	printf("#attrs:%d  #pages:%d  #tuples:%d  d:%d  sp:%d\n",
	       r->nattrs, r->npages, r->ntups, r->depth, r->sp);
	printf("Choice vector\n");
	printChVec(r->cv);
	printf("Bucket Info:\n");
	printf("%-4s %s\n","#","Info on pages in bucket");
	printf("%-4s %s\n","","(pageID,#tuples,freebytes,ovflow)");
	for (Offset pid = 0; pid < r->npages; pid++) {
		printf("[%2d]  ",pid);
		Page p = getPage(r->data, pid);
		Count ntups = pageNTuples(p);
		Count space = pageFreeSpace(p);
		Offset ovid = pageOvflow(p);
		printf("(d%d,%d,%d,%d)",pid,ntups,space,ovid);
		free(p);
		while (ovid != NO_PAGE) {
			Offset curid = ovid;
			p = getPage(r->ovflow, ovid);
			ntups = pageNTuples(p);
			space = pageFreeSpace(p);
			ovid = pageOvflow(p);
			printf(" -> (ov%d,%d,%d,%d)",curid,ntups,space,ovid);
			free(p);
		}
		putchar('\n');
	}
}
