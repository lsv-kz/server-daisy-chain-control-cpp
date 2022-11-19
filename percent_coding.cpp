#include "main.h"

using namespace std;

//======================================================================
int encode(const char *s_in, char *s_out, int len_out)
{
    unsigned char c,d;
    int cnt_o = 0;
    char *p = s_out;
    char Az09[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz" "0123456789" "/:-_.!~*'()";

    if((!s_in) || (!s_out))
        return 0;
    
    while((c = *s_in++))
    {
        if(c <= 0x7f)
        {
            if(!strchr(Az09, c))
            {
                if((cnt_o + 3) < len_out)
                {
                    *p++ = '%';
                    d = c >> 4;
                    *p++ = d < 10 ? d + '0' : d + '7';
                    d = c & 0x0f;
                    *p++ = d < 10 ? d + '0' : d + '7';
                    cnt_o += 3;
                }
                else
                {
                    *s_out = 0;
                    return 0;
                }
            }
            else if(c == ' ')
            {
                if((cnt_o + 1) < len_out)
                {
                    *p++ = '+';
                    cnt_o++;
                }
                else
                {
                    *s_out = 0;
                    return 0;
                }
            }
            else
            {
                if((cnt_o + 1) < len_out)
                {
                    *p++ = c;
                    cnt_o++;
                }
                else
                {
                    *s_out = 0;
                    return 0;
                }
            }
        }
        else
        {
            if((cnt_o + 3) < len_out)
            {
                *p++ = '%';
                d = c >> 4;
                *p++ = d < 10 ? d + '0' : d + '7';
                d = c & 0x0f;
                *p++ = d < 10 ? d + '0' : d + '7';
                cnt_o += 3;
            }
            else
            {
                *s_out = 0;
                return 0;
            }
        }
    }

    *p = 0;

    return cnt_o;
}
//======================================================================
int decode(const char *s_in, int len_in, char *s_out, int len)
{
    if (!s_in || !s_out)
        return -1;
    char tmp[3];
    char *p = s_out;
    unsigned char c;
    long cnt = 0, i;

    while (len_in > 0)
    {
        c = *(s_in++);
        --len_in;
        if (c == '%')
        {
            if (len_in < 2)
            {
                *p = 0;
                return 0;
            }
            
            tmp[0] = *(s_in++);
            tmp[1] = *(s_in++);
            tmp[2] = 0;
            len_in -= 2;

            const char* pp = tmp;
            i = strtol((char*)pp, (char**)&pp, 16);
            if (*pp != 0)
            {
                *p = 0;
                return 0;
            }
                
            *p = (char)i;
        }
        else if (c == '+')
            *p = ' ';
        else
            *p = c;

        --len;
         ++cnt;
        if (len <= 0)
        {
            *p = 0;
            return 0;
        }
        ++p;
    }

    *p = 0;
    return cnt;
}
