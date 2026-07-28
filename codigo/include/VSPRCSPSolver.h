#pragma once
#include "bcModelingLanguageC.hpp"
#include "bcModelRCSPSolver.hpp"
#include "VSPData.hpp"
#include <map>
#include <vector>
 
class VSPRCSPSolver {
public:
    VSPRCSPSolver(BcFormulation spForm,
                  int terminalId,
                  VSPData& data,
                  BcConstrArray& tripCov,
                  BcConstrArray& singleShiftLimit);
 
    ~VSPRCSPSolver() {
#ifdef BCP_RCSP_IS_FOUND
        if (oracle) delete oracle;
#endif
    }
 
#ifdef BCP_RCSP_IS_FOUND
    BcRCSPFunctor* getOracle() { return oracle; }
#endif
 
private:
    struct NodeKey {
        int fase;
        int viaje_id;
    };
 
    struct NodeKeyComparator {
        bool operator()(const NodeKey& a, const NodeKey& b) const {
            if (a.fase != b.fase) return a.fase < b.fase;
            return a.viaje_id < b.viaje_id;
        }
    };
 
    BcFormulation spForm;
    int terminalId;
    VSPData& data;
    BcConstrArray& tripCoverage;
    BcConstrArray& singleShiftLimit;
    BcVarArray tripVars;
    BcVarArray costVar;
    BcVarArray singleShiftVar;
 
    std::map<NodeKey, int, NodeKeyComparator> nodeIdMap;
 
    int nextNodeId;
    int sourceId;
    int sinkId;
 
    // Nodos puente antes del SINK — 3 niveles de penalización por duración.
    // Nivel 1: ventana estrecha, costo bajo  (ruta cerca del ideal)
    // Nivel 2: ventana media,   costo medio
    // Nivel 3: ventana ancha,   costo alto   (cubre el rango factible completo)
    static constexpr int N_NIVELES_PUENTE = 3;
    int puenteFullId[N_NIVELES_PUENTE];      // doble turno zona BAJA  [lb, ideal]
    int puenteFullId_arr[N_NIVELES_PUENTE];  // doble turno zona ALTA  [ideal, ub]
    int puenteSingleId[N_NIVELES_PUENTE];    // single-shift
 
#ifdef BCP_RCSP_IS_FOUND
    BcRCSPFunctor* oracle;
#endif
 
    int createOrGetNode(BcNetwork& network,
                        BcNetworkResource& tiempo_turno,
                        BcNetworkResource& tiempo_muerto,
                        int fase, int viaje_id);
 
    void buildVertices(BcNetwork& network,
                       BcNetworkResource& tiempo_turno,
                       BcNetworkResource& tiempo_muerto);
 
    void buildArcs(BcNetwork& network,
                   BcNetworkResource& tiempo_turno,
                   BcNetworkResource& tiempo_muerto);
};
