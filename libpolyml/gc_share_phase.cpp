/*
    Title:      Multi-Threaded Garbage Collector - Data sharing phase

    Copyright (c) 2012 David C. J. Matthews

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.
    
    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.
    
    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

*/

/*  GC Sharing pass.
    This pass is invoked only if the heap sizing code detects that heap space
    is running very short because it adds a very considerable overhead to GC.
    It aims to reduce the size of the live data in a similar way to the data
    sharing function PolyML.shareCommonData by merging immutable cells that
    contain data that cannot be distinguished.  Unlike the full sharing function
    this only combines cells that have identical contents although repeated calls
    will merge more complicated data structures.  The first time it is called
    identical leaves will be merged and the next time cells that refer to
    these leaves will be merged since the addresses will now be the same,
    and so on.  The full sharing code requires two additional words for
    each cell in the original heap but since this is likely to be called
    when memory is short we cannot afford that overhead.

    This code first does a full pass over the heap creating lists of cells
    that could possibly be merged.  The heads of the lists are held in
    hash tables.  There are separate hash tables for each combination
    of byte and word object and each possible object length from 1 to 10.
    Larger objects are not considered.  Because all the items in a list
    have the same length and type (flag bits) we can use the length word
    to link the items in the list.  A consequence of this is that positive
    long precision values can be shared but negative values cannot.  If
    two cells are to be shared they must have the same contents which
    implies that they must have the same length, type and hash value.
    Once the lists have been constructed each in turn is turned into
    a vector and sorted, bringing together cells with the same contents.
    Finally cells with the same contents are merged by choosing one cell
    and setting the forwarding pointers on the other cells to point to
    that.  This relies on the updating process in the mark phase of the
    GC to update addresses to cells that have been merged.  The sorting
    and forwarding process can be parallelised because each list is
    independent.  The reason for using the hash tables is both to
    spread the load for parallel processing and also to reduce
    the size of the vectors that need to be allocated for sorting,
    important because this pass is invoked when memory is short.
*/
#ifdef HAVE_CONFIG_H
#include "config.h"
#elif defined(_WIN32)
#include "winconfig.h"
#else
#error "No configuration file"
#endif

#ifdef HAVE_ASSERT_H
#include <assert.h>
#define ASSERT(x)   assert(x)
#else
#define ASSERT(x)
#endif

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#include "globals.h"
#include "processes.h"
#include "gc.h"
#include "scanaddrs.h"
#include "bitmap.h"
#include "memmgr.h"
#include "diagnostics.h"
#include "gctaskfarm.h"
#include "heapsizing.h"

class ObjEntry
{
public:
    ObjEntry(): objList(0), objCount(0), shareCount(0) {}
    PolyObject *objList;
    POLYUNSIGNED objCount;
    POLYUNSIGNED shareCount;
};

// There is an instance of this class for each combination of size and
// word/byte.
class SortVector
{
public:
    void AddToVector(PolyObject *obj, POLYUNSIGNED length);

    void SortData(void);
    POLYUNSIGNED Count() const;
    POLYUNSIGNED Shared() const;
    void SetLengthWord(POLYUNSIGNED l) { lengthWord = l; }

private:
    static int CompareItems(PolyObject* const *a,  PolyObject* const *b);

    static int qsCompare(const void *a, const void *b)
        { return CompareItems((PolyObject* const *)a, (PolyObject* const *)b); }

    static void sharingTask(GCTaskId*, void *a, void *b);

    // Items are pre-sorted by the first byte of the data.
    // This is mainly to create plenty of tasks for parallel
    // processing.
    ObjEntry objEntries[256];

    POLYUNSIGNED lengthWord;
};

POLYUNSIGNED SortVector::Count() const
{
    POLYUNSIGNED tableCount = 0;
    for (unsigned i = 0; i < 256; i++)
        tableCount += objEntries[i].objCount;
    return tableCount; 
}

POLYUNSIGNED SortVector::Shared() const
{
    POLYUNSIGNED shareCount = 0;
    for (unsigned i = 0; i < 256; i++)
        shareCount += objEntries[i].shareCount;
    return shareCount;
}

void SortVector::AddToVector(PolyObject *obj, POLYUNSIGNED length)
{
    /* Each object of a particular size is added to a hash table.
       The idea is to spread the work evenly between the tasks when
       we come to sort the entries.  We merge entries with identical
       contents which means they must have the same has value.  Word
       objects containing addresses are likely to be fairly well
       distributed whatever hash function is used but strings probably
       only contain ASCII characters. */
    unsigned char hash = 0;
    for(POLYUNSIGNED i = 0; i < length*sizeof(PolyWord); i++)
        hash += obj->AsBytePtr()[i];
    ObjEntry *entry = &objEntries[hash & 0xff];
    obj->SetShareChain(entry->objList);
    entry->objList = obj;
    entry->objCount++;
}

// The number of byte and word entries.
// Objects of up to and including this size are shared.
#define NUM_BYTE_VECTORS    10
#define NUM_WORD_VECTORS    10

class GetSharing: public RecursiveScanWithStack
{
public:
    GetSharing();
    void SortData(void);

protected:
    virtual bool TestForScan(PolyWord *);
    virtual void MarkAsScanning(PolyObject *);
    virtual void StackOverflow(void) {} // Ignore stack overflow
    virtual void Completed(PolyObject *);

    static void startSharing(GCTaskId*, void *a, void *b);

private:
    // The head of chains of cells of the same size
    SortVector byteVectors[NUM_BYTE_VECTORS];
    SortVector wordVectors[NUM_WORD_VECTORS];
};

GetSharing::GetSharing()
{
    for (unsigned i = 0; i < NUM_BYTE_VECTORS; i++)
        byteVectors[i].SetLengthWord((POLYUNSIGNED)(i+1) | _OBJ_BYTE_OBJ);

    for (unsigned j = 0; j < NUM_WORD_VECTORS; j++)
        wordVectors[j].SetLengthWord(j+1);
}

bool GetSharing::TestForScan(PolyWord *pt)
{
    PolyWord p = *pt;
    ASSERT(p.IsDataPtr());
    PolyObject *obj = p.AsObjPtr();

    while (obj->ContainsForwardingPtr())
    {
        obj = obj->GetForwardingPtr();
        *pt = obj;
    }
    ASSERT(obj == (*pt).AsObjPtr());

    LocalMemSpace *space = gMem.LocalSpaceForAddress(obj);

    if (space == 0)
        return false; // Ignore it if it points to a permanent area

    if (space->bitmap.TestBit(space->wordNo((PolyWord*)obj)))
        return false;

    return true;
}

void GetSharing::MarkAsScanning(PolyObject *obj)
{
    ASSERT(obj->ContainsNormalLengthWord());
    LocalMemSpace *space = gMem.LocalSpaceForAddress(obj);
    space->bitmap.SetBit(space->wordNo((PolyWord*)obj));
}

void GetSharing::Completed(PolyObject *obj)
{
    POLYUNSIGNED L = obj->LengthWord();
    // We have tables for word objects and byte objects
    // We chain entries together using the length word so it
    // is important that we only do this for objects that
    // have no other bits in the header, such as the sign bit.
    if ((L & _OBJ_PRIVATE_FLAGS_MASK) == 0)
    {
        POLYUNSIGNED length = obj->Length();
        ASSERT(length != 0);
        if (length <= NUM_WORD_VECTORS)
            wordVectors[length-1].AddToVector(obj, length);
    }
    else if ((L & _OBJ_PRIVATE_FLAGS_MASK) == F_BYTE_OBJ)
    {
        POLYUNSIGNED length = obj->Length();
        ASSERT(length != 0);
        if (length <= NUM_BYTE_VECTORS)
            byteVectors[length-1].AddToVector(obj, length);
    }
}

int SortVector::CompareItems(PolyObject * const *a, PolyObject * const *b)
{
    POLYUNSIGNED l1 = (*a)->LengthWord(), l2 = (*b)->LengthWord();
    if (l1 < l2)
        return -1;
    else if (l1 > l2)
        return 1;
    else return memcmp(*a, *b, OBJ_OBJECT_LENGTH(l1)*sizeof(PolyWord));
}

void SortVector::sharingTask(GCTaskId*, void *a, void *b)
{
    SortVector *s = (SortVector*)a;
    ObjEntry *oentry = (ObjEntry*)b;

    POLYUNSIGNED vecSize = oentry->objCount;
    ASSERT(vecSize > 2);

    // We could sort the list directly but it's much more efficient
    // to build an array and sort that.

    PolyObject **objTable = (PolyObject **)malloc(vecSize * sizeof(PolyObject*));

    POLYUNSIGNED p = 0;
    // Create the table and restore the length words.  They're
    // needed for the comparison function when we sort.
    // N.B. We must do this even if we failed to allocate the table.
    for (PolyObject *obj = oentry->objList; obj != 0; p++)
    {
        PolyObject *next = obj->GetShareChain();
        obj->SetLengthWord(s->lengthWord);
        if (objTable != 0)
            objTable[p] = obj;
        obj = next;
    }
    ASSERT(p == vecSize);

    if (objTable == 0)
        return;

    qsort(objTable, vecSize, sizeof(PolyObject*), qsCompare);

    PolyObject *share = objTable[0];

    for (POLYUNSIGNED i = 1; i < vecSize; i++)
    {
        if (CompareItems(&share, objTable+i) == 0)
        {
            objTable[i]->SetForwardingPtr(share);
            oentry->shareCount++;
        }
        else share = objTable[i];
    }

    free(objTable);
}

void SortVector::SortData()
{
    for (unsigned j = 0; j < 256; j++)
    {
        ObjEntry *oentry = &objEntries[j];

        // Sort this entry.  If it's very small just process it now.
        switch (oentry->objCount)
        {
        case 0: break; // Nothing there

        case 1: // Singleton - just restore the length word
            oentry->objList->SetLengthWord(lengthWord);
            break;

        case 2:
            {
                // Two items - process now
                PolyObject *obj1 = oentry->objList;
                PolyObject *obj2 = obj1->GetShareChain();
                obj1->SetLengthWord(lengthWord);
                if (CompareItems(&obj1, &obj2) == 0)
                {
                    obj2->SetForwardingPtr(obj1);
                    oentry->shareCount = 1;
                }
                else
                    obj2->SetLengthWord(lengthWord);
                break;
            }

        default:
            gpTaskFarm->AddWorkOrRunNow(sharingTask, this, oentry);
        }
    }
}

void GetSharing::startSharing(GCTaskId*, void *a, void *b)
{
    GetSharing *s = (GetSharing *)a;

    for (unsigned i = 0; i < NUM_BYTE_VECTORS; i++)
        s->byteVectors[i].SortData();

    for (unsigned j = 0; j < NUM_WORD_VECTORS; j++)
        s->wordVectors[j].SortData();
}


void GetSharing::SortData()
{
    // Process each of the entries.  We fork this as a separate task
    // purely to ensure that we only have as many threads working as
    // we've given in --gcthreads.
    gpTaskFarm->AddWorkOrRunNow(startSharing, this, 0);
    gpTaskFarm->WaitForCompletion();

    // Calculate the totals.
    POLYUNSIGNED totalSize = 0, totalShared = 0, totalRecovered = 0;
    for (unsigned k = 0; k < NUM_BYTE_VECTORS; k++)
    {
        totalSize += byteVectors[k].Count();
        POLYUNSIGNED shared = byteVectors[k].Shared();
        totalShared += shared;
        totalRecovered += shared * (k+2); // Add 2 because the 0th item is one word + length word.
    }

    for (unsigned l = 0; l < NUM_WORD_VECTORS; l++)
    {
        totalSize += wordVectors[l].Count();
        POLYUNSIGNED shared = wordVectors[l].Shared();
        totalShared += shared;
        totalRecovered += shared * (l+2);
    }

    if (debugOptions & DEBUG_GC)
        Log("GC: Share: Totals %" POLYUFMT " objects, %" POLYUFMT " shared (%1.0f%%).  %" POLYUFMT " words recovered.\n",
            totalSize, totalShared, (double)totalShared / (double)totalSize * 100.0, totalRecovered);

    gHeapSizeParameters.RecordSharingData(totalRecovered);
}

void GCSharingPhase(void)
{
    mainThreadPhase = MTP_GCPHASESHARING;

    GetSharing sharer;

    for (unsigned i = 0; i < gMem.nlSpaces; i++)
    {
        LocalMemSpace *lSpace = gMem.lSpaces[i];
        lSpace->bitmap.ClearBits(0, lSpace->spaceSize());
    }

    // Process the permanent mutable areas.
    for (unsigned j = 0; j < gMem.npSpaces; j++)
    {
        PermanentMemSpace *space = gMem.pSpaces[j];
        if (space->isMutable && ! space->byteOnly)
            sharer.ScanAddressesInRegion(space->bottom, space->top);
    }

    // Process the RTS roots.
    GCModules(&sharer);

    gHeapSizeParameters.RecordGCTime(HeapSizeParameters::GCTimeIntermediate, "Table");

    // Sort and merge the data.
    sharer.SortData();

    gHeapSizeParameters.RecordGCTime(HeapSizeParameters::GCTimeIntermediate, "Sort");
}
