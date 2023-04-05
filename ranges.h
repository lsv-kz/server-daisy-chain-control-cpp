#ifndef RANGES_H_
#define RANGES_H_

#include <cstdio>
#include <iostream>
#include <cstdlib>
#include <cstring>
//======================================================================
struct Range {
    long long start;
    long long end;
    long long len;
};
//----------------------------------------------------------------------
class ArrayRanges
{
protected:
    Range *range;
    unsigned int SizeArray, nRanges, i;
    long long sizeFile;
    int err;
    void check_ranges();
    void parse_ranges(char *sRange);

    void reserve()
    {
        if (err) return;
        if (SizeArray <= 0)
        {
            err = 1;
            return;
        }

        range = new(std::nothrow) Range [SizeArray];
        if (!range)
        {
            err = 1;
            return;
        }
    }

public:
    ArrayRanges()
    {
        err = 0;
        SizeArray = nRanges = i = 0;
        range = NULL;
    }

    ArrayRanges(const ArrayRanges&) = delete;

    ~ArrayRanges() { if (range) { delete [] range; } }

    void init(char *s, long long sz);

    ArrayRanges & operator << (const Range& val)
    {
        if (err) return *this;
        if (!range || (nRanges >= SizeArray))
        {
            err = 1;
            return *this;
        }

        range[nRanges++] = val;
        return *this;
    }

    Range *get()
    {
        if (err)
            return NULL;

        if (i < nRanges)
        {
            return range + (i++);
        }
        else
            return NULL;
    }

    void set_index() { i = 0; }
    int size() { if (err) return 0; return nRanges; }
    int capacity() { if (err) return 0; return SizeArray; }
    int error() { return -err; }
};

#endif
