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
class Ranges
{
protected:
    Range *range;
    unsigned int SizeArray, nRanges, index;
    long long sizeFile;
    int err;
    void check_ranges();
    void parse_ranges(char *sRange);

    void reserve(unsigned int n)
    {
        if (err) return;
        if (n == 0)
        {
            err = 1;
            return;
        }
        else if (n <= SizeArray)
            return;
        SizeArray = n;
        if (range)
            delete [] range;

        range = new(std::nothrow) Range [SizeArray];
        if (!range)
        {
            err = 1;
            return;
        }
    }

public:
    Ranges()
    {
        err = 0;
        SizeArray = nRanges = index = 0;
        range = NULL;
    }

    Ranges(const Ranges&) = delete;

    ~Ranges()
    {
        if (range)
            delete [] range;
    }

    void init(char *s, long long sz);

    Ranges & operator << (const Range& val)
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

        if (index < nRanges)
            return range + (index++);
        else
            return NULL;
    }

    void set_index() { index = 0; }
    int size() { if (err) return 0; return nRanges; }
    int capacity() { if (err) return 0; return SizeArray; }
    int error() { return -err; }
};

#endif
