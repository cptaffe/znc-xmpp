#include <znc/Utils.h>

class IRCCode{
public:
    IRCCode() {}
    IRCCode(unsigned int code, CString name) {
        m_Code = code;
        m_Name = name;
    }
    IRCCode(const IRCCode &other) {
        m_Code = other.m_Code;
        m_Name = other.m_Name;
    }

    unsigned int GetCode() const { return m_Code; }
    CString GetName() const { return m_Name; }
    bool IsClientError() const { return m_Code >= 400 && m_Code < 500; }
    bool IsServerError() const { return m_Code >= 500 && m_Code < 600; }

    static const IRCCode &FindCode(unsigned int code) {
        return vCodes[code];
    }
protected:
    unsigned int m_Code;
    CString m_Name;
    static std::map<unsigned int, IRCCode> vCodes;
};
