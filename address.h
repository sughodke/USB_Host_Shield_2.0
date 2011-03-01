#if !defined(__ADDRESS_H__)
#define __ADDRESS_H__

#include <inttypes.h>
#include "max3421e.h"

/* Endpoint information structure               */
/* bToggle of endpoint 0 initialized to 0xff    */
/* during enumeration bToggle is set to 00      */
typedef struct 
{        
    uint8_t			epAddr;					//copy from endpoint descriptor. Bit 7 indicates direction ( ignored for control endpoints )
    uint8_t			Attr;					// Endpoint transfer type.
    unsigned int	MaxPktSize;				// Maximum packet size.
    uint8_t			Interval;				// Polling interval in frames.
    uint8_t			sndToggle;				//last toggle value, bitmask for HCTL toggle bits
    uint8_t			rcvToggle;				//last toggle value, bitmask for HCTL toggle bits
    /* not sure if both are necessary */
} EP_RECORD;

//	  7   6   5   4   3   2   1   0
//  ---------------------------------
//  |   | H | P | P | P | A | A | A |
//  ---------------------------------
//
// H - if 1 the address is a hub address
// P - parent hub address
// A - device address / port number in case of hub
//
struct UsbDeviceAddress
{
	union
	{
		struct
		{
			uint8_t		bmAddress	: 3;	// device address/port number
			uint8_t		bmParent	: 3;	// parent hub address
			uint8_t		bmHub		: 1;	// hub flag
			uint8_t		bmReserved	: 1;	// reserved, must be zerro
		};
		uint8_t	devAddress;
	};
};

#define bmUSB_DEV_ADDR_ADDRESS		0x07
#define bmUSB_DEV_ADDR_PARENT		0x38
#define bmUSB_DEV_ADDR_HUB			0x40

struct UsbDevice
{
	EP_RECORD		*epinfo;		// endpoint info pointer
	uint8_t			address;		// address
	uint8_t			devclass;		// device class
};

class AddressPool
{
public:
	virtual uint8_t AllocAddress(UsbDeviceAddress parent, bool is_hub = false, uint8_t port = 0) = 0;
	virtual void FreeAddress(UsbDeviceAddress addr) = 0;
};

typedef void (*UsbDeviceHandleFunc)(UsbDevice *pdev);

#define ADDR_ERROR_INVALID_INDEX		0xFF
#define ADDR_ERROR_INVALID_ADDRESS		0xFF

template <const uint8_t MAX_DEVICES_ALLOWED>
class AddressPoolImpl
{
	EP_RECORD	dev0ep;						//Endpoint data structure used during enumeration for uninitialized device

	uint8_t		hubCounter;					// hub counter is kept 
											// in order to avoid hub address duplication

	UsbDevice	thePool[MAX_DEVICES_ALLOWED];

	// Initializes address pool entry
	void InitEntry(uint8_t index)
	{
		thePool[index].address	= 0;
		thePool[index].devclass = 0;
		thePool[index].epinfo	= &dev0ep;
	};
	// Returns thePool index for a given address
	uint8_t FindAddressIndex(uint8_t address = 0)
	{
		for (uint8_t i=1; i<MAX_DEVICES_ALLOWED; i++)
		{
			if (thePool[i].address == address)
				return i;
		}
		return 0;
	};
	// Returns thePool child index for a given parent
	uint8_t FindChildIndex(UsbDeviceAddress addr, uint8_t start = 1)
	{
		for (uint8_t i=(start<1 || start>=MAX_DEVICES_ALLOWED) ? 1 : start; i<MAX_DEVICES_ALLOWED; i++)
		{
			if (((UsbDeviceAddress*)&thePool[i].address)->bmParent == addr.bmAddress)
				return i;
		}
		return 0;
	};
	// Frees address entry specified by index parameter
	void FreeAddressByIndex(uint8_t index)
	{
		// Zerro field is reserved and should not be affected
		if (index == 0)
			return;

		// If a hub was switched off all port addresses should be freed
		if (((UsbDeviceAddress*)&thePool[index].address)->bmHub == 1)
		{
			for (uint8_t i=1; i = FindChildIndex(*((UsbDeviceAddress*)&thePool[index].address), i); )
				FreeAddressByIndex(i);

			// If the hub had the last allocated address, hubCounter should be decremented
			if (hubCounter == ((UsbDeviceAddress*)&thePool[index].address)->bmAddress)
				hubCounter --;
		}
		InitEntry(index);
	}
	// Initializes the whole address pool at once
	void InitAllAddresses()
	{
		for (uint8_t i=1; i<MAX_DEVICES_ALLOWED; i++)
			InitEntry(i);

		hubCounter = 0;
	};
	
public:
	AddressPoolImpl() : hubCounter(0)
	{
		// Zerro address is reserved
		InitEntry(0);

		thePool[0].address = 0;
		thePool[0].epinfo	= &dev0ep;
		dev0ep.epAddr		= 0;
		dev0ep.MaxPktSize	= 8;
		dev0ep.sndToggle	= bmSNDTOG0;   //set DATA0/1 toggles to 0
		dev0ep.rcvToggle	= bmRCVTOG0;

		InitAllAddresses();
	};
	// Returns a pointer to a specified address entry
	UsbDevice* GetUsbDevicePtr(uint8_t addr)
	{
		if (!addr)
			return thePool;

		uint8_t index = FindAddressIndex(addr);

		return (!index) ? NULL : thePool + index;
	};
	
	// Performs an operation specified by pfunc for each addressed device
	void ForEachUsbDevice(UsbDeviceHandleFunc pfunc)
	{
		if (!pfunc)
			return;

		for (uint8_t i=1; i<MAX_DEVICES_ALLOWED; i++)
			if (thePool[i].address)
				pfunc(thePool + i);
	};
	// Allocates new address
	virtual uint8_t AllocAddress(uint8_t parent, bool is_hub = false, uint8_t port = 0)
	{
		if (parent > 127 || port > 7)
			return 0;

		// finds first empty address entry starting from one
		uint8_t index = FindAddressIndex(0);	

		Serial.println(index, DEC);

		if (!index)					// if empty entry is not found
			return 0;

		if (parent == 0)
		{
			thePool[index].address = (is_hub) ? 0x41 : 1;
			return thePool[index].address;
		}

		UsbDeviceAddress	addr;

		addr.bmParent = ((UsbDeviceAddress*)&parent)->bmAddress;

		if (is_hub)
		{
			addr.bmHub		= 1;
			addr.bmAddress	= ++hubCounter;
		}
		else
		{
			addr.bmHub		= 0;
			addr.bmAddress	= port;
		}
		thePool[index].address = *((uint8_t*)&addr);

		return thePool[index].address;
	};
	// Empties pool entry
	virtual void FreeAddress(uint8_t addr)
	{
		// if the root hub is disconnected all the addresses should be initialized
		if (addr == 0x41)
		{
			InitAllAddresses();
			return;
		}
		uint8_t index = FindAddressIndex(addr);
		FreeAddressByIndex(index);
	};
	// Returns number of hubs attached
	// It can be rather helpfull to find out if there are hubs attached than getting the exact number of hubs.
	uint8_t GetNumHubs()
	{
		return hubCounter;
	};
	uint8_t GetNumDevices()
	{
		uint8_t counter = 0;

		for (uint8_t i=1; i<MAX_DEVICES_ALLOWED; i++)
			if (thePool[i].address != 0);
				counter ++;

		return counter;
	};
};

#endif // __ADDRESS_H__