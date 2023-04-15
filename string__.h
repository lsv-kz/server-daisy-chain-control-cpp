#ifndef CLASS_STRING_H_
#define CLASS_STRING_H_

#include <iostream>
#include <string>
//======================================================================
class BaseHex {};
class BaseDec {};

#define Hex BaseHex()
#define Dec BaseDec()
//======================================================================
class String
{
    std::string buf;
    int base_ = 10;
    unsigned int ind_ = 0;
    int err = 0;
    //------------------------------------------------------------------
    int is_space(char c)
    {
        switch (c)
        {
            case '\x20':
            case '\t':
            case '\r':
            case '\n':
            case '\0':
                return 1;
        }
        return 0;
    }
    //------------------------------------------------------------------
    void append(char ch)
    {
        buf += ch;
    }
    //------------------------------------------------------------------
    void append(const char *s)
    {
        if (s)
            buf += s;
    }
    //------------------------------------------------------------------
    void append(char *s)
    {
        if (s)
            buf += s;
    }
    //------------------------------------------------------------------
    void append(std::string& arg)
    {
        buf += arg;
    }
    //------------------------------------------------------------------
    void append(String& arg)
    {
        buf += arg.buf;
    }
    //------------------------------------------------------------------
    template <typename T>
    void append(T t)
    {
        const unsigned long size_ = 21;
        char s[size_];
        int cnt, minus = (t < 0) ? 1 : 0;
        const char *get_char = "FEDCBA9876543210123456789ABCDEF";
        if (base_ == 16)
            cnt = sizeof(t)*2;
        else
            cnt = 20;

        s[cnt] = 0;
        while (cnt > 0)
        {
            --cnt;
            if (base_ == 10)
            {
                s[cnt] = get_char[15 + (t % 10)];
                t /= 10;
            }
            else
            {
                s[cnt] = get_char[15 + (t & 0x0f)];
                t = t>>4;
            }
            if (t == 0)
                break;
        }

        if (base_ == 10)
            if (minus)
                s[--cnt] = '-';
        buf += (s + cnt);
    }
    //------------------------------------------------------------------
    int get_part_str(char *s, int max_len)
    {
        if (((ind_) && !is_space(buf[ind_])) || err)
            return (err = 1);
        unsigned int len = buf.size();
        for (; ind_ < len; ++ind_)
            if (!is_space(buf[ind_]))
                break;

        int i = 0;
        for ( ; (i < max_len) && (ind_ < len); ind_++)
        {
            char c = buf[ind_];
            if (c == '\r')
                continue;

            if (is_space(c))
            {
                s[i] = 0;
                return 0;
            }

            if (((base_ == 10) && (!isdigit(c))) && (c != '-'))
                return (err = 2);

            if ((base_ == 16) && (!isxdigit(c)))
                return (err = 3);

            s[i++] = c;
        }

        s[i] = 0;
        if ((ind_ < len) && (!is_space(buf[ind_])))
        {
            fprintf(stderr, "<%s:%d> \"We are not here. It is not us.\"\n", __func__, __LINE__);
            return (err = 4);
        }

        return 0;
    }

public:
    String(){}
    explicit String(unsigned int n) { buf.reserve(n); }
    /*String(const String& s)
    {
        buf = s.buf;
    }*/

    String(const std::string& s) { buf = s; }

    String& operator >> (double&) = delete;
    String& operator >> (char*) = delete;
    //------------------------------------------------------------------
    String& operator << (BaseHex b)
    {
        base_ = 16;
        return *this;
    }

    String& operator << (BaseDec b)
    {
        base_ = 10;
        return *this;
    }
    //------------------------------------------------------------------
    String & operator = (const char *s)
    {
        if (s)
        {
            buf.clear();
            buf += s;
        }
        return *this;
    }
    //------------------------------------------------------------------
    String & operator = (const std::string& s)
    {
        buf.clear();
        buf += s;
        return *this;
    }
    //------------------------------------------------------------------
    String& operator = (const String& s)
    {
        buf.clear();
        buf += s.buf;
        return *this;
    }
    //------------------------------------------------------------------
    friend bool operator == (const String & s1, const char *s2)
    {
        if (s1.buf == s2)
            return true;
        else
            return false;
    }
    //------------------------------------------------------------------
    String & operator += (char c)
    {
        buf += c;
        return *this;
    }
    //------------------------------------------------------------------
    const char operator[] (unsigned int n) const
    {
        if (n >= buf.size()) return '\0';
        return buf[n];
    }
    //------------------------------------------------------------------
    void append(char *s, int n)
    {
        int len = strlen(s);
        if (n <= len)
            buf.append(s, n);
    }
    //------------------------------------------------------------------
    template <typename T>
    String& operator << (T t)
    {
        ind_ = 0;
        append(t);
        return *this;
    }
    //------------------------------------------------------------------
    String& operator >> (std::string& s)
    {
        unsigned int len = buf.size();
        for (; ind_ < len; ++ind_)
            if (!is_space(buf[ind_]))
                break;
        s.clear();
        for (; ind_ < len; ind_++)
        {
            char c = buf[ind_];
            if (is_space(c))
                break;
            s += c;
        }
        return *this;
    }
    //------------------------------------------------------------------
    String& operator >> (String& s)
    {
        unsigned int len = buf.size();
        for (; ind_ < len; ++ind_)
            if (!is_space(buf[ind_]))
                break;
        s.clear();
        for (; ind_ < len; ind_++)
        {
            char c = buf[ind_];
            if (is_space(c))
                break;
            s += c;
        }
        return *this;
    }
    //------------------------------------------------------------------
    String& operator >> (long long& ll)
    {
        ll = 0;
        int max_len = 20;
        char s[21];

        if (base_ == 16)
            max_len = sizeof(long long) * 2;
        else
            if (buf[ind_] != '-')
                max_len = 19;

        if (get_part_str(s, max_len) == 0)
        {
    std::cout << " s   [" << s << "]\n";
            ll = strtoll(s, NULL, base_);
        }
        return *this;
    }
    //------------------------------------------------------------------
    String& operator >> (unsigned int& li)
    {
        li = 0;
        int max_len = 11;
        char s[12];

        if (base_ == 16)
            max_len = sizeof(int) * 2;
        else
            if (buf[ind_] != '-')
                max_len = 10;

        if (get_part_str(s, max_len) == 0)
            li = strtol(s, NULL, base_);
        return *this;
    }
    //------------------------------------------------------------------
    String& operator >> (int& li)
    {
        li = 0;
        int max_len = 11;
        char s[12];

        if (base_ == 16)
            max_len = sizeof(int) * 2;
        else
            if (buf[ind_] != '-')
                max_len = 10;

        if (get_part_str(s, max_len) == 0)
            li = strtol(s, NULL, base_);
        return *this;
    }
    //------------------------------------------------------------------
    String& operator >> (long& li)
    {
        li = 0;
        int max_len = 11;
        char s[12];

        if (base_ == 16)
            max_len = sizeof(long) * 2;
        else
            if (buf[ind_] != '-')
                max_len = 10;

        if (get_part_str(s, max_len) == 0)
            li = strtol(s, NULL, base_);
        return *this;
    }
    //------------------------------------------------------------------
    const char *c_str() const
    {
        return buf.c_str();
    }

    const std::string& str() const
    {
        return buf;
    }

    void resize(unsigned int n) { buf.resize(n); }
    int size() { return buf.size(); }
    int capacity() { return buf.capacity(); }
    void reserve(int n) { buf.reserve(n); }
    void clear() { buf = ""; ind_ = 0; err = 0; }
    int error() { return err; }
};

#endif
