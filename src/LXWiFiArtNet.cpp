/**************************************************************************/
/*!
    @file     LXWiFiArtNet.cpp
    @author   Claude Heintz
    @license  BSD (see LXDMXWiFi.h)
    @copyright 2015-2016 by Claude Heintz All Rights Reserved
    
    Art-Net(TM) Designed by and Copyright Artistic Licence Holdings Ltd.

    Supports sending and receiving Art-Net via ESP8266 WiFi connection.

    @section  HISTORY

    v1.0 - First release
    v1.1 - adds ability to use external packet buffer
    v1.2 - fixes cancel merge
    v1.2 - adds setLocalAddress
    v1.3 - adds ArtIpProg / ArtIpProgReply
*/
/**************************************************************************/

#include "LXWiFiArtNet.h"

//static buffer for sending poll replies
uint8_t LXWiFiArtNet::_reply_buffer[ARTNET_REPLY_SIZE];

LXWiFiArtNet::LXWiFiArtNet ( IPAddress address )
{	
    initialize(0);
    setLocalAddress(address);
    _my_subnetmask = INADDR_NONE;
    _broadcast_address = INADDR_NONE;
}

LXWiFiArtNet::LXWiFiArtNet ( IPAddress address, IPAddress subnet_mask )
{
    initialize(0);
    setLocalAddressMask(address, subnet_mask);
    uint32_t a = (uint32_t) address;
    uint32_t s = (uint32_t) subnet_mask;
    _broadcast_address = IPAddress(a | ~s);
}

LXWiFiArtNet::LXWiFiArtNet ( IPAddress address, IPAddress subnet_mask, uint8_t* buffer )
{
    initialize(buffer);
    setLocalAddress(address);
    uint32_t a = (uint32_t) address;
    uint32_t s = (uint32_t) subnet_mask;
    _broadcast_address = IPAddress(a | ~s);
}

LXWiFiArtNet::~LXWiFiArtNet ( void )
{
	if ( _owns_buffer ) {		// if we created this buffer, then free the memory
		free(_packet_buffer);
	}
}

void  LXWiFiArtNet::initialize  ( uint8_t* b ) {
	if ( b == 0 ) {
		// create buffer
		_packet_buffer = (uint8_t*) malloc(ARTNET_BUFFER_MAX);
		_owns_buffer = 1;
	} else {
		// external buffer
		_packet_buffer = b;
		_owns_buffer = 0;
	}
	
	//zero buffer including _dmx_data[0] which is start code
    for (int n=0; n<ARTNET_BUFFER_MAX; n++) {
    	_packet_buffer[n] = 0;
    	if ( n < DMX_UNIVERSE_SIZE ) {
    	   _dmx_buffer_a[n] = 0;
	   	_dmx_buffer_b[n] = 0;
	   	_dmx_buffer_c[n] = 0;
    	}
    }
    
    _dmx_slots = 0;
    _dmx_slots_a = 0;
    _dmx_slots_b = 0;
    _portaddress_lo = 0;
    _portaddress_hi = 0;
    
    _status1 = ARTNET_STATUS1_PORT_PROG;
    _status2 = ARTNET_STATUS2_ARTNET3_CAPABLE;
    
    _dmx_sender_a = INADDR_NONE;
    _dmx_sender_b = INADDR_NONE;
    _sequence = 1;
    _poll_reply_counter = 0;
    
    strcpy(_short_name, "ESP-DMX");
    strcpy(_long_name, "com.claudeheintzdesign.esp-dmx");
    _artaddress_receive_callback = 0;
    _artip_receive_callback = 0;
}

uint16_t  LXWiFiArtNet::universe ( void ) {
	return _portaddress_lo + ( _portaddress_hi << 8 );
}

void LXWiFiArtNet::setUniverse ( uint16_t u ) {
	_portaddress_lo = (u & 0xff);
	_portaddress_hi = ((u >> 8) & 0xff);
}

void LXWiFiArtNet::setSubnetUniverse ( uint8_t s, uint8_t u ) {
   _portaddress_lo = ((s & 0x0f) << 4) | ( u & 0x0f);
}

void LXWiFiArtNet::setUniverseAddress ( uint8_t u ) {
	if ( u != ARTADDRESS_NO_CHANGE ) {
	   if ( u & ARTADDRESS_PROG_BIT ) {
	     _portaddress_lo = (_portaddress_lo & 0xf0) | (u & 0x0f);
	   }
	}
}

void LXWiFiArtNet::setSubnetAddress ( uint8_t u ) {
	if ( u != ARTADDRESS_NO_CHANGE ) {
	   if ( u & ARTADDRESS_PROG_BIT ) {
	     _portaddress_lo = (_portaddress_lo & 0x0f) | ((u & 0x0f) << 4);
	   }
	}
}

void LXWiFiArtNet::setNetAddress   ( uint8_t u ) {
	if ( u != ARTADDRESS_NO_CHANGE ) {
	   if ( u & ARTADDRESS_PROG_BIT ) {
	     _portaddress_hi = u & 0x7f;
	   }
	}
}

int  LXWiFiArtNet::numberOfSlots ( void ) {
	return _dmx_slots;
}

void LXWiFiArtNet::setNumberOfSlots ( int n ) {
	_dmx_slots = n;
}

uint8_t LXWiFiArtNet::getSlot ( int slot ) {
	return _dmx_buffer_c[slot-1];
}

void LXWiFiArtNet::setSlot ( int slot, uint8_t level ) {
	_packet_buffer[ARTNET_ADDRESS_OFFSET+slot] = level;
}

uint8_t* LXWiFiArtNet::dmxData( void ) {
	return &_packet_buffer[ARTNET_ADDRESS_OFFSET+1];
}

uint8_t* LXWiFiArtNet::packetBuffer( void ) {
	return &_packet_buffer[0];
}

uint16_t LXWiFiArtNet::packetSize( void ) {
	return _packetSize;
}

uint8_t* LXWiFiArtNet::replyData( void ) {
	return &_reply_buffer[0];
}

char* LXWiFiArtNet::shortName( void ) {
	return &_short_name[0];
}

char* LXWiFiArtNet::longName( void ) {
	return &_long_name[0];
}

uint8_t LXWiFiArtNet::readDMXPacket ( UDP* wUDP ) {
	_packetSize = 0;
   uint16_t opcode = readArtNetPacket(wUDP);
   if ( opcode == ARTNET_ART_DMX ) {
   	return RESULT_DMX_RECEIVED;
   }
   return RESULT_NONE;
}

uint8_t LXWiFiArtNet::readDMXPacketContents ( UDP* wUDP, uint16_t packetSize ) {
	uint16_t opcode = readArtNetPacketContents(wUDP, packetSize);
   if ( opcode == ARTNET_ART_DMX ) {
   	return RESULT_DMX_RECEIVED;
   }
   if ( opcode == ARTNET_ART_POLL ) {
   	return RESULT_PACKET_COMPLETE;
   }
   return RESULT_NONE;
}

/*
  attempts to read a packet from the supplied EthernetUDP object
  returns opcode
  sends ArtPollReply with IPAddress if packet is ArtPoll
  replies directly to sender unless reply_ip != INADDR_NONE allowing specification of broadcast
  only returns ARTNET_ART_DMX if packet contained dmx data for this universe
  Packet size checks that packet is >= expected size to allow zero termination or padding
*/

uint16_t LXWiFiArtNet::readArtNetPacket ( UDP* wUDP ) {
	uint16_t packetSize = wUDP->parsePacket();
	uint16_t opcode = ARTNET_NOP;
	if ( packetSize ) {
		_packetSize = wUDP->read(_packet_buffer, ARTNET_BUFFER_MAX);
		opcode = readArtNetPacketContents(wUDP, _packetSize);
	}
	return opcode;
}
      

uint16_t LXWiFiArtNet::readArtNetPacketContents ( UDP* wUDP, uint16_t packetSize ) {
   uint16_t opcode = ARTNET_NOP;

	uint16_t t_slots = 0;
	/* Buffer now may not contain dmx data for desired universe.
		After reading the packet into the buffer, check to make sure
		that it is an Art-Net packet and retrieve the opcode that
		tells what kind of message it is.                            */
	opcode = parse_header();
	switch ( opcode ) {
		case ARTNET_ART_DMX:
			// ignore sequence[12] and physical[13]
			if ( ( _packet_buffer[14] == _portaddress_lo ) && ( _packet_buffer[15] == _portaddress_hi ) && ( _packet_buffer[11] >= 14 )) { //protocol version [10] hi byte [11] lo byte 
				packetSize -= 18;
				uint16_t slots = _packet_buffer[17] + (_packet_buffer[16] << 8);
				if ( packetSize >= slots ) {
					if ( (uint32_t)_dmx_sender_a == 0 ) {		//if first sender, remember address
						_dmx_sender_a = wUDP->remoteIP();
						for(int j=0; j<DMX_UNIVERSE_SIZE; j++) {
							_dmx_buffer_b[j] = 0;	//insure clear buffer 'b' so cancel merge works properly
						}
					}
					if ( _dmx_sender_a == wUDP->remoteIP() ) {
						_dmx_slots_a  = slots;
						if ( _dmx_slots_a > _dmx_slots_b ) {
							t_slots = _dmx_slots_a;
						} else {
							t_slots = _dmx_slots_b;
						}
						int di;
						int dt = ARTNET_ADDRESS_OFFSET + 1;
						  for (di=0; di<t_slots; di++) {
						    if ( di < slots ) {								// total slots may be greater than slots in this packet
							 	_dmx_buffer_a[di] = _packet_buffer[dt+di];
							}  else {										// don't read beyond end of received slots
								_dmx_buffer_a[di] = 0;						// set remainder to zero	
							}
							if ( _dmx_buffer_a[di] > _dmx_buffer_b[di] ) {
								_dmx_buffer_c[di] = _dmx_buffer_a[di];
							} else {
								_dmx_buffer_c[di] = _dmx_buffer_b[di];
							}
						  }
					} else { 												// did not match sender a
						if ( (uint32_t)_dmx_sender_b == 0 ) {		// if 2nd sender, remember address
							_dmx_sender_b = wUDP->remoteIP();
						}
						if ( _dmx_sender_b == wUDP->remoteIP() ) {
						  _dmx_slots_b  = slots;
							if ( _dmx_slots_a > _dmx_slots_b ) {
								t_slots = _dmx_slots_a;
							} else {
								t_slots = _dmx_slots_b;
							}
						  int di;
						  int dt = ARTNET_ADDRESS_OFFSET + 1;
						  for (di=0; di<t_slots; di++) {
							if ( di < slots ) {								//total slots may be greater than slots in this packet				
							 	_dmx_buffer_b[di] = _packet_buffer[dt+di];
							}  else {											//don't read beyond end of received slots	
								_dmx_buffer_b[di] = 0;							//set remainder to zero	
							}
							 if ( _dmx_buffer_a[di] > _dmx_buffer_b[di] ) {
								_dmx_buffer_c[di] = _dmx_buffer_a[di];
							 } else {
								_dmx_buffer_c[di] = _dmx_buffer_b[di];
							 }
						  }
						  
						}  // matched sender b
					}     // did not match sender a
				}		   // matched size
			}			   // matched universe
			if ( t_slots == 0 ) {	//only set >0 if all of above matched
				opcode = ARTNET_NOP;
			} else {
				_dmx_slots = t_slots;
			}
			break;
		case ARTNET_ART_ADDRESS:
			if (( packetSize >= 107 ) && ( _packet_buffer[11] >= 14 )) {  //protocol version [10] hi byte [11] lo byte
				opcode = parse_art_address();
				send_art_poll_reply( wUDP );
			}
			break;
		case ARTNET_ART_POLL:
			if (( packetSize >= 14 ) && ( _packet_buffer[11] >= 14 )) {
				send_art_poll_reply( wUDP );
			}
			break;
		case ARTNET_ART_IPPROG:
		   if (( packetSize >= 33 ) && ( _packet_buffer[11] >= 14 )) {
				parse_art_ipprog( wUDP );
			}
			break;
	}
   return opcode;
}

void LXWiFiArtNet::sendDMX ( UDP* wUDP, IPAddress to_ip, IPAddress interfaceAddr ) {
   strcpy((char*)_packet_buffer, "Art-Net");
   _packet_buffer[8] = 0;        //op code lo-hi
   _packet_buffer[9] = 0x50;
   _packet_buffer[10] = 0;
   _packet_buffer[11] = 14;
   if ( _sequence == 0 ) {
     _sequence = 1;
   } else {
     _sequence++;
   }
   _packet_buffer[12] = _sequence;
   _packet_buffer[13] = 0;
   _packet_buffer[14] = _portaddress_lo;
   _packet_buffer[15] = _portaddress_hi;
   _packet_buffer[16] = _dmx_slots >> 8;
   _packet_buffer[17] = _dmx_slots & 0xFF;
   //assume dmx data has been set
  
   wUDP->beginPacket(to_ip, ARTNET_PORT);
   wUDP->write(_packet_buffer, _dmx_slots+18);
   wUDP->endPacket();
}

/*
  sends ArtDMX packet to EthernetUDP object's remoteIP if to_ip is not specified
  ( remoteIP is set when parsePacket() is called )
  includes my_ip as address of this node
*/
void LXWiFiArtNet::send_art_poll_reply( UDP* wUDP ) {
  _reply_buffer[182] = 128;  // sending DMX flag
  
  _poll_reply_counter++;
  if ( _poll_reply_counter > 9999 ) {
  	 _poll_reply_counter = 0;
  }
  for (int k=108; k<172; k++) {
  	_reply_buffer[k] = 0;			// zero status string
  }
  sprintf((char*)&_reply_buffer[108], "#0001 [%04d] ", _poll_reply_counter);
  
  if ( _dmx_sender_a != INADDR_NONE ) {
    sprintf((char*)&_reply_buffer[121], "ArtDMX");
    if ( _dmx_sender_b != INADDR_NONE ) {
      sprintf((char*)&_reply_buffer[127], ", 2 Sources");
      _reply_buffer[182] |= 0x08;  //  merging
    }
  } else {
    sprintf((char*)&_reply_buffer[121], "Idle: no ArtDMX");
  }
  
  strcpy((char*)&_reply_buffer[26], _short_name);
  strcpy((char*)&_reply_buffer[44], _long_name);
  _reply_buffer[18] = _portaddress_hi;
  _reply_buffer[19] = _portaddress_lo >> 4;
  
  _reply_buffer[190] = _portaddress_lo & 0x0f;
  
  IPAddress a = _broadcast_address;
  if ( a == INADDR_NONE ) {
    a = wUDP->remoteIP();   // reply directly if no broadcast address is supplied
  }
  wUDP->beginPacket(a, ARTNET_PORT);
  wUDP->write(_reply_buffer, ARTNET_REPLY_SIZE);
  wUDP->endPacket();
}

void LXWiFiArtNet::send_art_ipprog_reply ( UDP* wUDP ) {
   _packet_buffer[8] = 0x00;        // op code lo-hi
   _packet_buffer[9] = 0xF9;
	IPAddress a = wUDP->remoteIP();
	wUDP->beginPacket(a, ARTNET_PORT);
   wUDP->write(_packet_buffer, ARTNET_IPPROG_SIZE);
   wUDP->endPacket();
}

void LXWiFiArtNet::setArtAddressReceivedCallback(ArtAddressRecvCallback callback) {
	_artaddress_receive_callback = callback;
}

void LXWiFiArtNet::setArtIpProgReceivedCallback(ArtIpProgRecvCallback callback) {
	_artip_receive_callback = callback;
}

uint16_t LXWiFiArtNet::parse_header( void ) {
  if ( strcmp((const char*)_packet_buffer, "Art-Net") == 0 ) {
    return _packet_buffer[9] * 256 + _packet_buffer[8];  //opcode lo byte first
  }
  return ARTNET_NOP;
}

/*
  reads an ARTNET_ART_ADDRESS packet
  can set output universe
  can cancel merge which resets address of dmx sender
     (after first ArtDmx packet, only packets from the same sender are accepted
     until a cancel merge command is received)
*/
uint16_t LXWiFiArtNet::parse_art_address( void ) {
	//[14] to [31] short name <= 18 bytes
	//[32] to [95] long name  <= 64 bytes
	//[96][97][98][99]                  input universe   ch 1 to 4
	//[100][101][102][103]               output universe   ch 1 to 4
	if ( _packet_buffer[14] != 0 ) {
		strcpy(_short_name, (char*) &_packet_buffer[14]);
	}
	if ( _packet_buffer[32] != 0 ) {
		strcpy(_long_name, (char*) &_packet_buffer[32]);
	}
	
	setNetAddress(_packet_buffer[12]);
	setUniverseAddress(_packet_buffer[100]);
	//[104] subnet
	setSubnetAddress(_packet_buffer[104]);
	//[105] reserved
	uint8_t command = _packet_buffer[106]; // command
	switch ( command ) {
	   case 0x00:
	      if ( _artaddress_receive_callback != NULL ) {	//notify settings may have changed
				_artaddress_receive_callback();
			}
			break;
	   case 0x01:	//cancel merge: resets ip address used to identify dmx sender
	   	_dmx_sender_a = INADDR_NONE;
	   	_dmx_sender_b = INADDR_NONE;
	   	break;
	   case 0x90:	//clear buffer
	   	_dmx_sender_a = INADDR_NONE;
	   	_dmx_sender_b = INADDR_NONE;
	   	for(int j=0; j<DMX_UNIVERSE_SIZE; j++) {
	   	   _dmx_buffer_a[j] = 0;
	   	   _dmx_buffer_b[j] = 0;
	   	   _dmx_buffer_c[j] = 0;
	   	}
	   	_dmx_slots = 512;
	   	return ARTNET_ART_DMX;	// return ARTNET_ART_DMX so function calling readPacket
	   	   						   // knows there has been a change in levels
	   	break;
	}
	
	return ARTNET_ART_ADDRESS;
}

void LXWiFiArtNet::parse_art_ipprog( UDP* wUDP ) {
   uint8_t cmd = _packet_buffer[14];
   if ( cmd & 0x80 ) {
		if ( _artip_receive_callback != NULL ) {
		   IPAddress ipaddr = (_packet_buffer[19] << 24) | (_packet_buffer[18] << 16) | (_packet_buffer[17] << 8) | _packet_buffer[16];
		   IPAddress subnet = (_packet_buffer[23] << 24) | (_packet_buffer[22] << 16) | (_packet_buffer[21] << 8) | _packet_buffer[20];
			_artip_receive_callback( cmd, ipaddr, subnet );
		}
		send_art_ipprog_reply( wUDP );
	} else {	//info only, reply with ArtIPProgReply
		if ( _status2 & ARTNET_STATUS2_DHCP_USED ) {
			_packet_buffer[26] = 0x40;
		} else {
			_packet_buffer[26] = 0x00;
		}
		_packet_buffer[16] = ((uint32_t)_my_address) & 0xff;      //ip address
		_packet_buffer[17] = ((uint32_t)_my_address) >> 8;
		_packet_buffer[18] = ((uint32_t)_my_address) >> 16;
		_packet_buffer[19] = ((uint32_t)_my_address) >>24;
		_packet_buffer[20] = ((uint32_t)_my_subnetmask) & 0xff;   //subnet mask
		_packet_buffer[21] = ((uint32_t)_my_subnetmask) >> 8;
		_packet_buffer[22] = ((uint32_t)_my_subnetmask) >> 16;
		_packet_buffer[23] = ((uint32_t)_my_subnetmask) >>24;
		send_art_ipprog_reply( wUDP );
	}
}

void LXWiFiArtNet::setLocalAddress ( IPAddress address ) {
	_my_address = address;
	initializePollReply();
}

void  LXWiFiArtNet::setLocalAddressMask ( IPAddress address, IPAddress subnet_mask ) {
	_my_address = address;
	_my_subnetmask = subnet_mask;
	initializePollReply();
}

void LXWiFiArtNet::setStatus1Flag ( uint8_t flag, uint8_t set ) {
	if ( set ) {
		_status1 |= flag;
	} else {
		_status1 &= ~flag;
	}
	_reply_buffer[23] = _status1;
}

void  LXWiFiArtNet::setStatus2Flag ( uint8_t flag, uint8_t set ) {
	if ( set ) {
		_status2 |= flag;
	} else {
		_status2 &= ~flag;
	}
	_reply_buffer[212] = _status2;
}

void  LXWiFiArtNet::initializePollReply  ( void ) {
	int i;
  for ( i = 0; i < ARTNET_REPLY_SIZE; i++ ) {
    _reply_buffer[i] = 0;
  }
  strcpy((char*)_reply_buffer, "Art-Net");
  _reply_buffer[8] = 0;        // op code lo-hi
  _reply_buffer[9] = 0x21;
  _reply_buffer[10] = ((uint32_t)_my_address) & 0xff;      //ip address
  _reply_buffer[11] = ((uint32_t)_my_address) >> 8;
  _reply_buffer[12] = ((uint32_t)_my_address) >> 16;
  _reply_buffer[13] = ((uint32_t)_my_address) >>24;
  _reply_buffer[14] = 0x36;    // port lo first always 0x1936
  _reply_buffer[15] = 0x19;
  _reply_buffer[16] = 0;       // firmware hi-lo
  _reply_buffer[17] = 0;
  _reply_buffer[18] = _portaddress_hi;		// net upper 7 bits of net-subnet-universe[p]
  _reply_buffer[19] = _portaddress_lo >> 4;  // subnet nibble of net-subnet-universe[p]
  _reply_buffer[20] = 0x12;    // oem hi-lo
  _reply_buffer[21] = 0x51;
  _reply_buffer[22] = 0;       // ubea
  _reply_buffer[23] = _status1;// status
  _reply_buffer[24] = 0x78;    //     Mfg Code
  _reply_buffer[25] = 0x6c;    //     seems DMX workshop reads these bytes backwards
  strcpy((char*)&_reply_buffer[26], _short_name);
  strcpy((char*)&_reply_buffer[44], _long_name);
  _reply_buffer[173] = 1;    // number of ports
  _reply_buffer[174] = 128;  // can output from network
  _reply_buffer[190] = _portaddress_lo & 0x0f;
  _reply_buffer[212] = _status2;
}