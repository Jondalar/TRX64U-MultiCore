// T163 S3: the board's FTP state - the values of the "Network Settings" store as ftpd.cc reads them.
#include "ftpstate.h"
#include "network_config.h"

FtpState ftp_state_now(void)
{
    FtpState s;
    s.on = networkConfig.cfg->get_value(CFG_NETWORK_FTP_SERVICE) != 0;
    s.port = FTPSTATE_PORT;
    const char *pw = networkConfig.cfg->get_string(CFG_NETWORK_PASSWORD);
    s.password = pw && *pw;
    return s;
}

void ftp_state_set(bool on)
{
    networkConfig.cfg->set_value(CFG_NETWORK_FTP_SERVICE, on ? 1 : 0);
    networkConfig.cfg->write();
    networkConfig.cfg->effectuate();
}
