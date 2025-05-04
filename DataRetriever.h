#ifndef DATARETRIEVER_H
#define DATARETRIEVER_H

#include "CanSDO.h"

class DataRetriever
{
   public:
      DataRetriever(CanSDO &canSDO);

   private:
      CanSDO &canSDO;

};

#endif
