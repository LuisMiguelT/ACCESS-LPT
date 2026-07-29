#include "VSPRCSPSolver.h"
#include "VSPData.hpp"
#include <iostream>
#include <cmath>
#include <iomanip>
#include <vector>
#include <fstream>

using namespace std;

const int RECURSO_TIEMPO_TURNO  = 0;
const int RECURSO_TIEMPO_MUERTO = 1;

// ============================================================================
// CONSTRUCTOR
// ============================================================================

VSPRCSPSolver::VSPRCSPSolver(BcFormulation spForm,
                             int terminalId,
                             VSPData& data,
                             BcConstrArray& tripCov,
                             BcConstrArray& singleShiftLimit)
    : spForm(std::move(spForm)),
      terminalId(terminalId),
      data(data),
      tripCoverage(tripCov),
      singleShiftLimit(singleShiftLimit),
      tripVars(this->spForm, "tripVar"),
      costVar(this->spForm, "costVar"),
      singleShiftVar(this->spForm, "singleShiftVar"),
      nextNodeId(0),
      sourceId(-1),
      sinkId(-1),
      puenteFullId{-1, -1, -1},
      puenteFullId_arr{-1, -1, -1},
      puenteSingleId{-1, -1, -1}
#ifdef BCP_RCSP_IS_FOUND
    , oracle(nullptr)
#endif
{
    cout << "  VSPRCSPSolver - con soporte single-shift y puentes SINK   " << endl;

    int viajes_terminal = 0;
    for (int j = 0; j < data.n; ++j)
        if (data.viajes[j].salida == terminalId || data.viajes[j].llegada == terminalId)
            viajes_terminal++;

    cout << "  Terminal: " << terminalId << "  Viajes: " << data.n
         << "  Tocan: " << viajes_terminal << endl;

    cout << "  Parámetros de grafo:" << endl;
    cout << "    F0 [" << data.fase_0_min << ", " << data.fase_0_max << "]" << endl;
    cout << "    F1 [" << data.fase_1_min << ", " << data.fase_1_max << "]" << endl;
    cout << "    F2 [" << data.fase_2_min << ", " << data.fase_2_max << "]" << endl;
    cout << "    F3 [" << data.fase_3_min << ", " << data.fase_3_max << "]" << endl;
    cout << "    max_tiempo_ruta    = " << data.max_tiempo_ruta      << endl;
    cout << "    espera intra-fase  = ["
         << data.min_espera_intra_fase << ", " << data.max_espera_intra_fase << "]" << endl;
    cout << "    espera transición  = ["
         << data.min_espera_transicion << ", " << data.max_espera_transicion << "]" << endl;
    cout << "    pen_ideal F0→F1=" << data.pen_ideal_f0_f1
         << "  F2→F3=" << data.pen_ideal_f2_f3 << endl;
    cout << "    costo_single_shift=" << data.costo_penalizacion_single_shift << endl;
    cout << "    dur_viaje_minima="   << data.dur_viaje_minima << endl;

    nodeIdMap.clear();

    BcNetwork network(this->spForm, data.n, 0, data.n);

    // -----------------------------------------------------------------------
    // Recurso 0: tiempo_turno — main, disposable
    // -----------------------------------------------------------------------
    BcNetworkResource tiempo_turno(network, RECURSO_TIEMPO_TURNO);
    tiempo_turno.setAsMainResource();

    // -----------------------------------------------------------------------
    // Recurso 1: tiempo_muerto — secundario, NON-disposable
    // -----------------------------------------------------------------------
    BcNetworkResource tiempo_muerto(network, RECURSO_TIEMPO_MUERTO);
    tiempo_muerto.setAsNonDisposableResource();

    // =========================================================
    // tripVars: una por viaje, binarias
    // =========================================================
    for (int j = 0; j < data.n; ++j) {
        tripVars(j).type('B');
        tripVars[j] >= 0.0;
        tripVars[j] <= 1.0;
        tripCoverage[j] += tripVars[j];
    }
    tripVars.priorityForMasterBranching(1.0);
    tripVars.priorityForSubproblemBranching(0.0);
    tripVars.priorityForRyanFosterBranching(0.0);
    cout << data.n << " tripVars" << endl;

    // =========================================================
    // singleShiftVar — 1 variable binaria por subproblema
    // =========================================================
    singleShiftVar(0).type('B');
    singleShiftVar[0] >= 0.0;
    singleShiftVar[0] <= 1.0;
    singleShiftLimit[0] += singleShiftVar[0];
    singleShiftVar.priorityForMasterBranching(0.0);
    singleShiftVar.priorityForSubproblemBranching(0.0);
    singleShiftVar.priorityForRyanFosterBranching(0.0);
    cout << "  singleShiftVar creada y ligada a constraint master" << endl;

    // =========================================================
    // costVar: acumula la suma de originalCost de arcos
    // =========================================================
    costVar(0).type('C');
    costVar[0] >= 0.0;

    BcObjective spObj(this->spForm);
    spObj += costVar[0];
    cout << "  costVar[0] creado y añadido al objetivo" << endl;

    // =========================================================
    // Construir grafo
    // =========================================================
    buildVertices(network, tiempo_turno, tiempo_muerto);
    buildArcs(network, tiempo_turno, tiempo_muerto);

    // =========================================================
    // Oracle RCSP
    // =========================================================
#ifdef BCP_RCSP_IS_FOUND
    oracle = new BcRCSPFunctor(this->spForm);
    oracle->setPureCostBcVar(costVar[0]);
    cout << " Oracle RCSP creado con setPureCostBcVar(costVar[0])" << endl;
#endif

    cout << "╚════════════════════════════════════════════════════════════╝" << endl;
}

// ============================================================================
// NODOS
// ============================================================================

int VSPRCSPSolver::createOrGetNode(BcNetwork& network,
                                   BcNetworkResource& tiempo_turno,
                                   BcNetworkResource& tiempo_muerto,
                                   int fase, int viaje_id) {
    NodeKey key = {fase, viaje_id};
    auto it = nodeIdMap.find(key);
    if (it != nodeIdMap.end()) return it->second;

    BcVertex vertex = network.createVertex();
    int nodeId = nextNodeId++;

    if (viaje_id >= 0 && viaje_id < data.n) {
        tiempo_turno.setVertexConsumptionLB(vertex, 0.0);
        if      (fase == 0) tiempo_turno.setVertexConsumptionUB(vertex, data.fase_0_max);
        else if (fase == 1) tiempo_turno.setVertexConsumptionUB(vertex, data.fase_1_max);
        else if (fase == 2) tiempo_turno.setVertexConsumptionUB(vertex, data.fase_2_max);
        else if (fase == 3) tiempo_turno.setVertexConsumptionUB(vertex, data.fase_3_max);

        tiempo_muerto.setVertexConsumptionUB(vertex, data.max_tiempo_ruta);
        if      (fase == 0) tiempo_muerto.setVertexConsumptionLB(vertex, data.fase_0_min);
        else if (fase == 1) tiempo_muerto.setVertexConsumptionLB(vertex, data.fase_1_min);
        else if (fase == 2) tiempo_muerto.setVertexConsumptionLB(vertex, data.fase_2_min);
        else if (fase == 3) tiempo_muerto.setVertexConsumptionLB(vertex, data.fase_3_min);

    } else {
        tiempo_turno.setVertexConsumptionLB(vertex, 0.0);
        tiempo_turno.setVertexConsumptionUB(vertex, data.max_tiempo_ruta);
        tiempo_muerto.setVertexConsumptionLB(vertex, 0.0);
        tiempo_muerto.setVertexConsumptionUB(vertex, data.max_tiempo_ruta);
    }

    if (viaje_id >= 0 && viaje_id < data.n) {
        vertex.setPackingSet(viaje_id);
    }

    nodeIdMap[key] = nodeId;
    return nodeId;
}

// ============================================================================
// VERTICES
// ============================================================================

void VSPRCSPSolver::buildVertices(BcNetwork& network,
                                   BcNetworkResource& tiempo_turno,
                                   BcNetworkResource& tiempo_muerto) {
    cout << "\n[VERTICES]" << endl;

    BcVertex source = network.createVertex();
    sourceId = nextNodeId++;
    tiempo_turno.setVertexConsumptionLB(source, 0.0);
    tiempo_turno.setVertexConsumptionUB(source, 0.0);
    tiempo_muerto.setVertexConsumptionLB(source, 0.0);
    tiempo_muerto.setVertexConsumptionUB(source, 0.0);
    network.setPathSource(source);
    nodeIdMap[{-1, -1}] = sourceId;

    // PUENTES FULL ZONA BAJA — tm ∈ [ideal - d_bajo[k], ideal]  costo=c_bajo[k]
    for (int k = 0; k < N_NIVELES_PUENTE; ++k) {
        BcVertex pf = network.createVertex();
        puenteFullId[k] = nextNodeId++;
        double lb = data.pen_dur_full_ideal - data.pen_dur_full_d_bajo[k];
        double ub = data.pen_dur_full_ideal;
        tiempo_turno.setVertexConsumptionLB(pf, 0.0);
        tiempo_turno.setVertexConsumptionUB(pf, data.max_tiempo_ruta);
        tiempo_muerto.setVertexConsumptionLB(pf, lb);
        tiempo_muerto.setVertexConsumptionUB(pf, ub);
        cout << "  puente_full_bajo[" << k << "]  ID=" << puenteFullId[k]
             << "  tm=[" << lb << "," << ub << "]"
             << "  c=" << data.pen_dur_full_c_bajo[k] << endl;
    }

    // PUENTES FULL ZONA ALTA — tm ∈ [ideal, ideal + d_arriba[k]]  costo=c_arriba[k]
    for (int k = 0; k < N_NIVELES_PUENTE; ++k) {
        BcVertex pf = network.createVertex();
        puenteFullId_arr[k] = nextNodeId++;
        double lb = data.pen_dur_full_ideal;
        double ub = data.pen_dur_full_ideal + data.pen_dur_full_d_arriba[k];
        tiempo_turno.setVertexConsumptionLB(pf, 0.0);
        tiempo_turno.setVertexConsumptionUB(pf, data.max_tiempo_ruta);
        tiempo_muerto.setVertexConsumptionLB(pf, lb);
        tiempo_muerto.setVertexConsumptionUB(pf, ub);
        cout << "  puente_full_arr[" << k << "]  ID=" << puenteFullId_arr[k]
             << "  tm=[" << lb << "," << ub << "]"
             << "  c=" << data.pen_dur_full_c_arriba[k] << endl;
    }

    // ------------------------------------------------------------------
    // PUENTES SINGLE — 3 niveles, centrados en pen_dur_single_ideal
    // ------------------------------------------------------------------
    for (int k = 0; k < N_NIVELES_PUENTE; ++k) {
        BcVertex ps = network.createVertex();
        puenteSingleId[k] = nextNodeId++;
        double lb = data.pen_dur_single_ideal - data.pen_dur_single_d[k];
        double ub = data.pen_dur_single_ideal + data.pen_dur_single_d[k];
        tiempo_turno.setVertexConsumptionLB(ps, 0.0);
        tiempo_turno.setVertexConsumptionUB(ps, data.max_tiempo_ruta);
        tiempo_muerto.setVertexConsumptionLB(ps, lb);
        tiempo_muerto.setVertexConsumptionUB(ps, ub);
        cout << "  puente_single[" << k << "] ID=" << puenteSingleId[k]
             << "  tm=[" << lb << "," << ub << "]"
             << "  costo=" << data.pen_dur_single_c[k] << endl;
    }

    // ------------------------------------------------------------------
    // SINK
    // ------------------------------------------------------------------
    BcVertex sink = network.createVertex();
    sinkId = nextNodeId++;
    tiempo_turno.setVertexConsumptionLB(sink, 0.0);
    tiempo_turno.setVertexConsumptionUB(sink, data.max_tiempo_ruta);
    tiempo_muerto.setVertexConsumptionLB(sink, 0.0);
    tiempo_muerto.setVertexConsumptionUB(sink, data.max_tiempo_ruta);
    network.setPathSink(sink);
    nodeIdMap[{-2, -2}] = sinkId;

    // ------------------------------------------------------------------
    // Nodos de viaje (4 fases × n viajes, con poda horaria)
    // ------------------------------------------------------------------
    int vpf[4] = {0, 0, 0, 0};
    for (int fase = 0; fase < 4; ++fase) {
        for (int j = 0; j < data.n; ++j) {
            double sal = data.viajes[j].inicio;

            if ((fase == 0 || fase == 1) &&
                data.poda_f01_sal_max >= 0 && sal > data.poda_f01_sal_max) continue;

            if ((fase == 2 || fase == 3) &&
                data.poda_f23_sal_min >= 0 && sal < data.poda_f23_sal_min) continue;

            createOrGetNode(network, tiempo_turno, tiempo_muerto, fase, j);
            vpf[fase]++;
        }
    }

    cout << "  SOURCE=" << sourceId << "  SINK=" << sinkId << endl;
    cout << "  puente_full_bajo[0..2]=" << puenteFullId[0]
         << ".." << puenteFullId[N_NIVELES_PUENTE-1] << endl;
    cout << "  puente_full_arr[0..2]=" << puenteFullId_arr[0]
         << ".." << puenteFullId_arr[N_NIVELES_PUENTE-1] << endl;
    cout << "  puente_single[0..2]=" << puenteSingleId[0]
         << ".." << puenteSingleId[N_NIVELES_PUENTE-1] << endl;
    cout << "  F0=" << vpf[0] << " F1=" << vpf[1]
         << " F2=" << vpf[2] << " F3=" << vpf[3]
         << "  TotalNodos=" << nextNodeId << endl;
}

// ============================================================================
// ARCOS
// ============================================================================

auto penDescanso = [](double x) -> double {
    if (x < 30.3)
        return 5.0 * (30.5 - x) * (30.5 - x);
    else
        return std::fabs(30.5 - x);
};

// Penalización intra-fase, zona ideal [5,10]
auto penIntraFase = [](double t) -> double {
    if (t >= 5.0 && t <= 10.0)  return 0.0;
    if (t < 2.0)                 return 45.0 - 15.0 * t;
    if (t < 5.0)                 return 5.0 * (5.0 - t);
    if (t <= 13.0)               return 5.0 * (t - 10.0);
    return 15.0 * t - 180.0;
};

void VSPRCSPSolver::buildArcs(BcNetwork& network,
                               BcNetworkResource& tiempo_turno,
                               BcNetworkResource& tiempo_muerto) {
    cout << "\n[ARCOS]" << endl;

    int ai = 0, aintra[4] = {0,0,0,0}, at01 = 0, at12 = 0, at23 = 0;
    int as_full = 0, as_single = 0;

    // ------------------------------------------------------------------
    // SOURCE -> F0(j)
    // ------------------------------------------------------------------
    for (int j = 0; j < data.n; ++j) {
        if (data.viajes[j].salida != terminalId) continue;
        auto it = nodeIdMap.find({0, j});
        if (it == nodeIdMap.end()) continue;
        int dur_j = data.viajes[j].fin - data.viajes[j].inicio;
        BcArc arc = network.createArc(sourceId, it->second, 0.0);
        tiempo_turno.setArcConsumption(arc, dur_j);
        tiempo_muerto.setArcConsumption(arc, 0.0);
        arc.arcVar(tripVars[j]);
        ai++;
    }

    // ------------------------------------------------------------------
    // INTRA-FASE: F(fase,i) -> F(fase,j)
    // ------------------------------------------------------------------
    for (int fase = 0; fase < 4; ++fase)
        for (int i = 0; i < data.n; ++i) {
            auto it_i = nodeIdMap.find({fase, i});
            if (it_i == nodeIdMap.end()) continue;
            for (int j = 0; j < data.n; ++j) {
                if (i == j) continue;
                if (data.viajes[i].llegada != data.viajes[j].salida) continue;
                double tm = data.viajes[j].inicio - data.viajes[i].fin;
                if (tm < data.min_espera_intra_fase ||
                    tm > data.max_espera_intra_fase) continue;
                auto it_j = nodeIdMap.find({fase, j});
                if (it_j == nodeIdMap.end()) continue;
                int dur_i = data.viajes[i].fin - data.viajes[i].inicio;
                int dur_j = data.viajes[j].fin - data.viajes[j].inicio;
                // LMT: Revisar el uso del tercer parámetro del createArc, que es el costo original del arco. En este caso, se está usando 0.0, pero podría ser diferente si se quiere penalizar de alguna manera.
                BcArc arc = network.createArc(it_i->second, it_j->second, 0.0);
                tiempo_turno.setArcConsumption(arc, tm + dur_j);
                tiempo_muerto.setArcConsumption(arc, tm + dur_i);
                arc.arcVar(tripVars[j]);
                arc.addVarAssociation(costVar[0], penIntraFase(tm));
                aintra[fase]++;
            }
        }

    // ------------------------------------------------------------------
    // F0 -> F1: primer descanso
    // ------------------------------------------------------------------
    for (int i = 0; i < data.n; ++i) {
        auto it_i = nodeIdMap.find({0, i});
        if (it_i == nodeIdMap.end()) continue;
        for (int j = 0; j < data.n; ++j) {
            if (data.viajes[i].llegada != data.viajes[j].salida) continue;
            auto it_j = nodeIdMap.find({1, j});
            if (it_j == nodeIdMap.end()) continue;
            double tm = data.viajes[j].inicio - data.viajes[i].fin;
            if (tm < data.min_espera_transicion ||
                tm > data.max_espera_transicion) continue;
            double pen = penDescanso(tm);
            int dur_i  = data.viajes[i].fin - data.viajes[i].inicio;
            int dur_j  = data.viajes[j].fin - data.viajes[j].inicio;
            BcArc arc  = network.createArc(it_i->second, it_j->second, 0.0);
            tiempo_turno.setArcConsumption(arc, tm + dur_j);
            tiempo_muerto.setArcConsumption(arc, tm + dur_i);
            arc.arcVar(tripVars[j]);
            arc.addVarAssociation(costVar[0], pen);
            at01++;
        }
    }

    // ------------------------------------------------------------------
    // F1 -> F2: cambio de turno (sin penalización)
    // ------------------------------------------------------------------
    for (int i = 0; i < data.n; ++i) {
        if (data.viajes[i].llegada != terminalId) continue;
        auto it_i = nodeIdMap.find({1, i});
        if (it_i == nodeIdMap.end()) continue;
        for (int j = 0; j < data.n; ++j) {
            if (data.viajes[j].salida != terminalId) continue;
            auto it_j = nodeIdMap.find({2, j});
            if (it_j == nodeIdMap.end()) continue;
            double tm = data.viajes[j].inicio - data.viajes[i].fin;
            if (tm < data.min_espera_cam_turno ||
                tm > data.max_espera_cam_turno) continue;
            int dur_i  = data.viajes[i].fin - data.viajes[i].inicio;
            int dur_j  = data.viajes[j].fin - data.viajes[j].inicio;
            BcArc arc  = network.createArc(it_i->second, it_j->second, 0.0);
            tiempo_turno.setArcConsumption(arc, dur_j);
            tiempo_muerto.setArcConsumption(arc, dur_i);
            arc.arcVar(tripVars[j]);
            at12++;
        }
    }

    // ------------------------------------------------------------------
    // F2 -> F3: segundo descanso
    // ------------------------------------------------------------------
    for (int i = 0; i < data.n; ++i) {
        auto it_i = nodeIdMap.find({2, i});
        if (it_i == nodeIdMap.end()) continue;
        for (int j = 0; j < data.n; ++j) {
            if (data.viajes[i].llegada != data.viajes[j].salida) continue;
            auto it_j = nodeIdMap.find({3, j});
            if (it_j == nodeIdMap.end()) continue;
            double tm = data.viajes[j].inicio - data.viajes[i].fin;
            if (tm < data.min_espera_transicion ||
                tm > data.max_espera_transicion) continue;
            double pen = penDescanso(tm);
            int dur_i  = data.viajes[i].fin - data.viajes[i].inicio;
            int dur_j  = data.viajes[j].fin - data.viajes[j].inicio;
            BcArc arc  = network.createArc(it_i->second, it_j->second, 0.0);
            tiempo_turno.setArcConsumption(arc, tm + dur_j);
            tiempo_muerto.setArcConsumption(arc, tm + dur_i);
            arc.arcVar(tripVars[j]);
            arc.addVarAssociation(costVar[0], pen);
            at23++;
        }
    }

    // ------------------------------------------------------------------
    // F3 -> puente_full[k] -> SINK  (rutas doble turno)
    //
    // Cada nivel tiene DOS nodos puente:
    //   puenteFullId[k]       → zona BAJA:  tm ∈ [lb, ideal]  costo=c_bajo[k]
    //   puenteFullId_arr[k]   → zona ALTA:  tm ∈ [ideal, ub]  costo=c_arriba[k]
    // El RCSP usa el nodo al que puede llegar — la ventana hace el filtro.
    // ------------------------------------------------------------------
    for (auto& e : nodeIdMap) {
        if (e.first.viaje_id < 0 || e.first.fase < 0) continue;
        if (e.first.fase != 3) continue;
        if (data.viajes[e.first.viaje_id].llegada != terminalId) continue;
        int dur_i = data.viajes[e.first.viaje_id].fin
                  - data.viajes[e.first.viaje_id].inicio;
        for (int k = 0; k < N_NIVELES_PUENTE; ++k) {
            // Zona baja: costo c_bajo
            BcArc arc_b = network.createArc(e.second, puenteFullId[k],
                                            data.pen_dur_full_c_bajo[k]);
            tiempo_turno.setArcConsumption(arc_b, 0.0);
            tiempo_muerto.setArcConsumption(arc_b, dur_i);
            // Zona alta: costo c_arriba
            BcArc arc_a = network.createArc(e.second, puenteFullId_arr[k],
                                            data.pen_dur_full_c_arriba[k]);
            tiempo_turno.setArcConsumption(arc_a, 0.0);
            tiempo_muerto.setArcConsumption(arc_a, dur_i);
            as_full++;
        }
    }
    for (int k = 0; k < N_NIVELES_PUENTE; ++k) {
        BcArc arc2b = network.createArc(puenteFullId[k],     sinkId, 0.0);
        tiempo_turno.setArcConsumption(arc2b, 0.0);
        tiempo_muerto.setArcConsumption(arc2b, 0.0);
        BcArc arc2a = network.createArc(puenteFullId_arr[k], sinkId, 0.0);
        tiempo_turno.setArcConsumption(arc2a, 0.0);
        tiempo_muerto.setArcConsumption(arc2a, 0.0);
    }

    // ------------------------------------------------------------------
    // F1 -> puente_single[k] -> SINK  (rutas single-shift)
    // ------------------------------------------------------------------
    for (auto& e : nodeIdMap) {
        if (e.first.viaje_id < 0 || e.first.fase < 0) continue;
        if (e.first.fase != 1) continue;
        if (data.viajes[e.first.viaje_id].llegada != terminalId) continue;
        int dur_i = data.viajes[e.first.viaje_id].fin
                  - data.viajes[e.first.viaje_id].inicio;
        for (int k = 0; k < N_NIVELES_PUENTE; ++k) {
            BcArc arc1 = network.createArc(e.second, puenteSingleId[k],
                                           data.costo_penalizacion_single_shift
                                           + data.pen_dur_single_c[k]);
            tiempo_turno.setArcConsumption(arc1, 0.0);
            tiempo_muerto.setArcConsumption(arc1, dur_i);
            arc1.arcVar(singleShiftVar[0]);
            as_single++;
        }
    }
    for (int k = 0; k < N_NIVELES_PUENTE; ++k) {
        BcArc arc2 = network.createArc(puenteSingleId[k], sinkId, 0.0);
        tiempo_turno.setArcConsumption(arc2, 0.0);
        tiempo_muerto.setArcConsumption(arc2, 0.0);
    }

    int total = ai
              + aintra[0] + aintra[1] + aintra[2] + aintra[3]
              + at01 + at12 + at23
              + as_full + as_single
              + 3 * N_NIVELES_PUENTE;  // full_bajo->SINK + full_arr->SINK + single->SINK

    cout << "  SOURCE->F0: " << ai << endl;
    for (int f = 0; f < 4; f++)
        cout << "  IntraF" << f << ": " << aintra[f] << endl;
    cout << "  Trans F0->F1: " << at01
         << "  F1->F2: "  << at12
         << "  F2->F3: "  << at23 << endl;
    cout << "  F3->puentes_full (x6 bajo+arr): " << as_full
         << "  F1->puentes_single (x3): " << as_single << endl;
    cout << "  Total: " << total << endl;
}
