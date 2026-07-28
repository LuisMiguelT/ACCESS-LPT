#ifndef VSPMODEL_HPP
#define VSPMODEL_HPP
 
#include "VSPData.hpp"
#include "bcModelingLanguageC.hpp"
 
// Forward declarations
class BcModel;
 
// Función para construir el modelo — expone colGenSp y master
// para que AppVSP pueda usarlos en el warm start
void buildVSPModel(VSPData& data, BcModel& vspModel,
                   BcColGenSpArray& colGenSp, BcMaster& master);
 
#endif
