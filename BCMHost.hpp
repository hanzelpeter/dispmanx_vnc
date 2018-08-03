#ifndef BCMHOST_HH
#define BCMHOST_HH

#include "bcm_host.h"

class BCMHost
{
public:
	BCMHost() {
		bcm_host_init();
	};
	~BCMHost() {
		bcm_host_deinit();
	}
};

#endif // BCMHOST_HH
