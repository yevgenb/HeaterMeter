#include <WProgram.h>
#include "strings.h"
#include "rfmanager.h"

void RFSource::setId(unsigned char id)
{
  _id = id;
  _lastReceive = 0;
  _batteryLevel = 0;
  _signalLevel = 0xff;
  //_nextSeq = 0;
  memset(Values, 0, sizeof(Values));
}

void RFSource::update(rf12_probe_update_hdr_t *hdr, unsigned char len)
{
  _batteryLevel = hdr->batteryLevel;
  if (_lastReceive != 0)
  {
    // _signalLevel is just a bitfield that shifts in a 1 for every packet
    // we get in sequence, and shifts out that 1 for every packet we miss
    // e.g. 01111111 indicates 7 of the past 8 packets received
    unsigned char seqDiff = hdr->seqNo - _nextSeq;
    if (seqDiff == 0)
    {
      _signalLevel = (_signalLevel << 1) | 1;
    }
    else 
    {
      _signalLevel = _signalLevel >> seqDiff;
    } 
  }  /* if have received before */
  
  _nextSeq = hdr->seqNo + 1;
  _lastReceive = millis();
  
  rf12_probe_update_t *probe = (rf12_probe_update_t *)&hdr[1];
  len -= sizeof(rf12_probe_update_hdr_t);
  while (len >= sizeof(rf12_probe_update_t))
  {
    Values[probe->probeIdx] = probe->adcValue;
    len -= sizeof(rf12_probe_update_t);
    //print_P(PSTR("RFM ")); Serial.print(probe->probeIdx, DEC); Serial_char(' '); Serial.print(probe->adcValue, DEC); Serial_nl();
    ++probe;
  }  /* while len */
}

RFManager::RFManager(char rxLed) :
  _crcOk(0xff), _rxLed(rxLed)
{
  if (_rxLed >= 0)
    pinMode(_rxLed, OUTPUT);
}

void RFManager::init(unsigned char band)
{
  // The master is always node 1
  rf12_initialize(1, band);
}

void RFManager::freeStaleSources(void)
{
  for (unsigned char idx=0; idx<RF_SOURCE_COUNT; ++idx)
    if (_sources[idx].isStale())
      _sources[idx].setId(RFSOURCEID_NONE);
}

char RFManager::findFreeSourceIdx(void)
{
  freeStaleSources();
  for (unsigned char idx=0; idx<RF_SOURCE_COUNT; ++idx)
    if (_sources[idx].isFree())
      return idx;
  return -1;
}

char RFManager::findSourceIdx(unsigned char srcId)
{
  // Get the index of the srcId, returns -1 if it doesn't exist
  for (unsigned char idx=0; idx<RF_SOURCE_COUNT; ++idx)
    if (_sources[idx].getId() == srcId)
      return idx;
  return -1;
}

char RFManager::forceSourceIdx(unsigned char srcId)
{
  // Get the index of the srcId, allocates it if it doesn't exist
  char retVal = findSourceIdx(srcId);
  if (retVal == -1)
  {
    retVal = findFreeSourceIdx();
    if (retVal != -1)
      _sources[retVal].setId(srcId);
  }
  return retVal;
}

RFSource *RFManager::getSourceById(unsigned char srcId)
{
  char idx = findSourceIdx(srcId);
  return (idx != -1) ? &_sources[idx] : NULL;
}

void RFManager::status(void)
{
  freeStaleSources();

  // The first item in the list the manager but it has the same format as 
  // the other sources, which is: Id,Signal,TimeSinceLastReceive
  Serial_char('A');
  Serial_csv();
  Serial.print((unsigned int)3300, DEC);  // battery level
  Serial_csv();
  Serial.print(_crcOk, DEC); // signal
  Serial_csv();
  Serial_char('0');   // last update

  unsigned long m = millis();  
  for (unsigned char idx=0; idx<RF_SOURCE_COUNT; ++idx)
  {
    if (_sources[idx].isFree())
      continue;
    Serial_csv();
    Serial_char('A' + _sources[idx].getId() - 1);
    Serial_csv();
    Serial.print(_sources[idx].getBatteryLevel(), DEC);
    Serial_csv();
    Serial.print(_sources[idx].getSignalLevel(), DEC);
    Serial_csv();
    
    unsigned int since = (m - _sources[idx].getLastReceive()) / 1000;
    Serial.print(since, DEC);
  }
}

boolean RFManager::doWork(void)
{
  boolean retVal = false;
  while (rf12_recvDone())
  {
    if (_rxLed >= 0)
      digitalWrite(_rxLed, HIGH);
    if ((rf12_crc == 0) && (rf12_len >= sizeof(rf12_probe_update_hdr_t)))
    {  
      if (_crcOk < 0xff) 
        ++_crcOk;
        
      // If this is a broadcast Iit should be a probe update
      if ((rf12_hdr & RF12_HDR_DST) != 0)
      {
        rf12_probe_update_hdr_t *hdr = (rf12_probe_update_hdr_t *)rf12_data;
        //Serial.print(rf12_hdr, HEX); Serial_nl();
        char src = forceSourceIdx(rf12_hdr & RF12_HDR_MASK);
        if (src != -1)
          _sources[src].update(hdr, rf12_len);
      }  /* if broadcast */
    }  /* if crc ok */
    else if (_crcOk > 0) 
      --_crcOk;
      
    retVal = true;
  }  /* while recvDone() */
  
  // Leave the LED on until we fail to read something
  if (!retVal && _rxLed >= 0)
    digitalWrite(_rxLed, LOW);
    
  return retVal;    
}


