#pragma once

#include <string>

static const std::string KEY_PREFIX = "KEY_";
static constexpr char VALUE_POOL[] =
        "l9hd62^dfh.pxrC6ZU79OnfCZ06DsNXWY3U4dt1JTGQVvylBdZSlHWXC4PokCxsMdjv8xRptFMMQyH12DEr7Zur"
        "tZpsyreMmPwuw7WMRnoN5wAYWtkqDwXyQlYb4RgtSc4xsonpTx2UhIUi15oJTx1CXAhmECacCQf4eeJ9Utus0N"
        "mKgEFV8qYDsNtJ21TkjCyCDhVIATkEugCw1BfNIB9AZDGiqXc0llp4rlJPl4bIG2QC4La3M1oh3yIGkolpdoYV"
        "er1dc9EfQz88q5Hv00g53Q3H6jcgicoY8YW5K4josd2e53ikesQi2kzqvTI9xxM5wtFexkFm8wFdTf37Hz204w"
        "fQErGZkdMnRxXJ5EJ627e7zuFuVtazb0L65B3nU5R9tyUl2bTZiDcakK9evrTXoTkbkGjkCOiMSTNCzlUZCxof"
        "jYQCLusRkXEm0CNVuifTnytctwLfKjwob4hJ0WxlQN9FV9Mm9zT61EQ8zEMrqr6hf7XMqhcQR7DWAKaspUaU6h"
        "oP2w3t3MktVaBp6MgS6Apbkb7EsihETHHqKFkKMCkYBbKfgsq7Jy49T1Wx2UJsD3XX03kVBbqRWmrjIKzqQEnN"
        "sd0f-0d09540936/.f/dfprjetroeijtte0985409609254fj/.s.1293@1!$43556oi5y6o54yi6ikjsdbfsd"
        ")(*7^hdjdyf484*^%$$3{}}{}{POI)_IUOHH&*^^&$Dydyutd65456djug(*_(*OIHJHKJBKJBKJB@A@A$gggg"
        "+_0-8435906IYWV79364ngkbo5m788*()M<:>><:Lpkdfgmgrtjyoyij5yj657i6509ju560ju560ju0uj0uj6"
        "NvhE8N7yR26fgHncH6QbsI10HyxlvYHKFiMk5nPNDbffueF2xKLzteSd0NazU2APkJWXvBW2oUu8dkZnWMM8TH"
        "vrUoLDWwMdh14MfxwmEQbtGnUHEpRktUB6b7JTJN8IHyxlvYHKFiMk5nPND()bueF2xKLz;|@f,t";

inline std::string BuildKey(uint32_t key_num) {
    char buf[16];
    sprintf(buf, "%0*d", 12, key_num);
    std::string result = KEY_PREFIX + std::string(buf, 12);
    return result;
}

inline std::string BuildValue(uint32_t length, std::string & key) {
    std::string result;
    result.reserve(length);
    result = "VALUE_" + key;

    length -= result.size();
    int cur_length = 0;
    while(cur_length < length) {
        int cut_len = std::min(size_t(length - cur_length), sizeof(VALUE_POOL));
        result.append(std::string(VALUE_POOL, cut_len));
        cur_length += cut_len;
    }

    return result;
}