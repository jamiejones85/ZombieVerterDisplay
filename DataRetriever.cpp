#include "DataRetriever.h"


DataRetriever::DataRetriever(CanSDO &canSDO, DisplayManager &displayManager) : canSDO(canSDO), displayManager(displayManager) {
}

void DataRetriever::GetNextValue() {

  //have we switched screens?
  int screenIndex = displayManager.GetScreenIndex();
  if (screenIndex != dataSetIndex) {
    dataSetIndex = screenIndex;
    dataIndex = 0;
  }

  int dataId = dataSet[dataSetIndex][dataIndex];
  int value = canSDO.GetValue(dataId);
  displayManager.UpdateData(dataId, value);

  dataIndex++;

  int dataCount = sizeof(dataSet[dataSetIndex])/sizeof(dataSet[dataSetIndex][0]);

  if (dataIndex >= dataCount) {
    dataIndex = 0;
  }
  
}
