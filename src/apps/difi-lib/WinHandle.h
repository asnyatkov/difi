#pragma once

// Barebone windows HANDLE RAII wrapper
class WinHandle
{
public:
    WinHandle(HANDLE h) :
        m_h(h)
    {
    }
    
    ~WinHandle()
    {
        if(m_h != INVALID_HANDLE_VALUE)
            ::CloseHandle(m_h);
    }
    
    bool operator !() const { return m_h == INVALID_HANDLE_VALUE; }
    operator HANDLE() const { return m_h; }

private:
    HANDLE m_h;
};
