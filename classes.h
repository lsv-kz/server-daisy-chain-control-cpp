#ifndef CLASSES_H_
#define CLASSES_H_

#include "main.h"

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
    Range *range = NULL;
    unsigned int SizeArray = 0;
    unsigned int nRanges = 0;
    long long sizeFile;
    int err = 0;
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
    ArrayRanges(const ArrayRanges&) = delete;
    ArrayRanges() = delete;
    ArrayRanges(char *s, long long sz);
    ~ArrayRanges() { if (range) { delete [] range; } }

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

    Range *get(unsigned int i)
    {
        if (err) return NULL;
        if (i < 0)
        {
            err = 1;
            return NULL;
        }

        if (i < nRanges)
            return range + i;
        else
            return NULL;
    }

    int size() { if (err) return 0; return nRanges; }
    int capacity() { if (err) return 0; return SizeArray; }
    int error() { return -err; }
};

#endif
