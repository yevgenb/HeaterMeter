#include <WProgram.h>
#include "strings.h"
#include "rfmanager.h"

struct __rfm12_probe_update_hdr {
  boolean lowBattery;
  unsigned char sourceId;
  unsigned char seqNo;
};
struct __rfm12_probe_update {
  unsigned char probeIdx;
  unsigned int adcValue;
};

void RFSource::setId(unsigned char id)
{
  _id = id;
  _lastReceive = 0;
  _lowBattery = false;
  _signalLevel = 0xff;
  //_nextSeq = 0;
}

void RFSource::update(struct __rfm12_probe_update_hdr *hdr, unsigned char len)
{
  _lowBattery = hdr->lowBattery;
  Serial.print(_lowBattery ? "LOB " : "OKB ");
  Serial.print(hdr->seqNo, DEC);
  Serial_char(' ');
  if (_lastReceive != 0)
  {
    unsigned char seqDiff = hdr->seqNo - _nextSeq;
    if (seqDiff == 0)
    {
      if (_signalLevel < 0xff) 
        ++_signalLevel;
    }
    else {
      if (seqDiff < _signalLevel)
        _signalLevel -= seqDiff;
      else
        _signalLevel = 0;
    } 
  }  /* if have received before */
  
  _nextSeq = hdr->seqNo + 1;
  _lastReceive = millis();
  
  struct __rfm12_probe_update *probe = (struct __rfm12_probe_update *)&hdr[1];
  len -= sizeof(struct __rfm12_probe_update_hdr);
  while (len >= sizeof(struct __rfm12_probe_update))
  {
    Values[probe->probeIdx] = probe->adcValue;
    Serial.print(probe->probeIdx, DEC); 
    Serial_char(':');
    Serial.print(probe->adcValue, DEC); 
    Serial_nl();
    len -= sizeof(struct __rfm12_probe_update);
    ++probe;
  }  /* while len */
}

void RFSource::doFree(void)
{
  _id = 0;
  memset(Values, 0, sizeof(Values));
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
      _sources[idx].doFree();
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
  Serial.print(_crcOk, DEC);
  Serial_csv();
  Serial_char('0');  

  unsigned long m = millis();  
  for (unsigned char idx=0; idx<RF_SOURCE_COUNT; ++idx)
  {
    if (_sources[idx].isFree())
      continue;
    Serial_csv();
    Serial_char('A' + _sources[idx].getId() - 1);
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
    if ((rf12_crc == 0) && (rf12_len >= sizeof(__rfm12_probe_update_hdr)))
    {  
      if (_crcOk < 0xff) 
        ++_crcOk;
      struct __rfm12_probe_update_hdr *hdr = (struct __rfm12_probe_update_hdr *)rf12_data;
      char src = forceSourceIdx(hdr->sourceId);
      if (src != -1)
        _sources[src].update(hdr, rf12_len);
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

