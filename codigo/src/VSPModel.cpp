#include "VSPModel.hpp"
#include "VSPRCSPSolver.h"
#include "bcModelingLanguageC.hpp"
#include <iostream>
 
using namespace std;
 
void buildVSPModel(VSPData& data, BcModel& vspModel,
                   BcColGenSpArray& colGenSp, BcMaster& master) {
    cout << "\n=== CONSTRUYENDO MODELO RCSP CON GRAFO MULTI-CAPA ===" << endl;
 
    if (data.n == 0) return;
 
    // =========================================================
    // 1. RESTRICCIONES MAESTRAS: UNA POR VIAJE
    // =========================================================
 
    cout << "\n--- Creando restricciones de cobertura ---" << endl;
    cout << "  Total viajes: " << data.n << endl;
 
    BcConstrArray tripCoverage(master, "TripCoverage");
    BcVarArray slackVar(master, "SlackNoCubierto");
 
    for(int i = 0; i < data.n; ++i) {
        slackVar(i).type('B');
        slackVar[i] >= 0.0;
        slackVar[i] <= 1.0;
 
        tripCoverage(i) == 1.0;
        tripCoverage(i) += slackVar[i];
    }
 
    cout << data.n << " restricciones de cobertura creadas" << endl;
 
    // =========================================================
    // RESTRICCIÓN MASTER PARA SINGLE-SHIFT
    //     Suma de singleShiftVar sobre todos los subproblemas <= K
    // =========================================================
    BcConstrArray singleShiftLimit(master, "SingleShiftLimit");
    singleShiftLimit(0) <= (double)data.max_rutas_single_shift;
    cout << "  Restricción single-shift creada: <= "
         << data.max_rutas_single_shift << endl;
 
    // =========================================================
    // 2. FUNCIÓN OBJETIVO
    // Un solo BcObjective: setea minInt, el costo artificial, y agrega slacks
    // CRÍTICO: setArtCostValue permite a BaPCod reconocer que la solución
    // del warm start (sin artificiales) es mejor que cualquier arranque con
    // artificiales, lo que activa las columnas en mastInitMode=6.
    // =========================================================
 
    BcObjective objective(master);
    objective.setMinMaxStatus(BcObjStatus::minFloat);
    objective.setArtCostValue(data.costo_columna_artificial);
    for(int i = 0; i < data.n; ++i) {
        objective += data.costo_columna_artificial * slackVar[i];
    }
 
    cout << " Función objetivo configurada sobre MASTER como ENTERA" << endl;
 
    // =========================================================
    // 3. SUBPROBLEMAS (uno por terminal)
    // =========================================================
 
    for (int k = 0; k < data.d; ++k) {
        cout << "\n--- Terminal " << k << " (ID: " << data.terminales[k].id
             << ", " << data.terminales[k].nombre << ") ---" << endl;
 
        BcFormulation subProb = colGenSp(k);
 
        int max_buses = data.terminales[k].num_buses > 0
                        ? data.terminales[k].num_buses
                        : 1000;
        subProb <= max_buses;
        subProb.setFixedCost(data.costo_fijo_ruta);
 
        VSPRCSPSolver* solver = new VSPRCSPSolver(
            subProb,
            data.terminales[k].id,
            data,
            tripCoverage,
            singleShiftLimit
        );
 
#ifdef BCP_RCSP_IS_FOUND
        subProb.attach(solver->getOracle());
        cout << "   RCSPSolver adjuntado al subproblema" << endl;
        cout << "   Límite de buses: " << max_buses << endl;
#else
        cerr << "   ERROR: BCP_RCSP no disponible" << endl;
        exit(1);
#endif
    }
 
    // =========================================================
    // 4. BRANCHING CONSTRAINTS BASADAS EN tripVars
    // =========================================================
 
    cout << "\n--- Configurando branching constraints ---" << endl;
 
    BcBranchingConstrArray tripBranching(
        master,
        "TripBranching",
        SelectionStrategy::MostFractional,
        2.0
    );
 
    for (int k = 0; k < data.d; ++k) {
        BcFormulation subProb = colGenSp[k];
        BcVarArray tripVars(subProb, "tripVar");
 
        for(int j = 0; j < data.n; ++j) {
            tripBranching(j) += tripVars[j];
        }
    }
 
    cout << data.n << " branching constraints creadas" << endl;
 
    cout << "\n Modelo construido exitosamente" << endl;
    cout << "   → solCost = costo_fijo_ruta * num_rutas + suma(tiempos_muertos + penalizaciones)" << endl;
    cout << "   → Grafo multi-capa con 4 fases + salida anticipada F1→sink" << endl;
    cout << "   → " << data.n << " restricciones de cobertura" << endl;
    cout << "   → " << data.n << " branching constraints" << endl;
    cout << "   → max_rutas_single_shift = " << data.max_rutas_single_shift << endl;
}
