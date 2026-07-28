#include "VSPData.hpp"
#include "VSPModel.hpp"
#include "bcModelingLanguageC.hpp"
#include <iostream>
#include <string>
#include <vector>
#include <set>
#include <map>
#include <climits>
#include <iomanip>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <memory>
#include <unordered_map>

using namespace std;

// ============================================================================
// Estructura para guardar la mejor solución entera encontrada
// ============================================================================
struct MejorSolucion {
    double costo = 1e18;
    int numRoutes = 0;
    int tripsCubiertos = 0;
    int slacks = 0;
    vector<int> slackIds;
    struct Ruta {
        string deposito;
        double costaTM;
        vector<int> trips;
    };
    vector<Ruta> rutas;

    void imprimir(const VSPData& data) const {
        auto m2t = [](int m) -> string {
            char buf[8];
            snprintf(buf, sizeof(buf), "%02d:%02d", m/60, m%60);
            return string(buf);
        };
        cout << "  MEJOR SOLUCIÓN ENTERA ENCONTRADA   costo = "
             << fixed << setprecision(2) << costo << "\n";
        for (int i = 0; i < (int)rutas.size(); i++) {
            const auto& r = rutas[i];
            cout << "  Ruta " << setw(3) << (i+1)
                 << " [" << r.deposito << "]"
                 << " TM=" << fixed << setprecision(1) << r.costaTM
                 << "  #viajes=" << r.trips.size() << ":";
            for (int tid : r.trips) {
                auto& vj = data.viajes[tid];
                cout << " " << tid
                     << "(" << m2t(vj.inicio) << "-" << m2t(vj.fin) << ")";
            }
            cout << "\n";
        }
        cout << "\n  RESUMEN \n"
             << "  Rutas:             " << numRoutes << "\n"
             << "  Viajes cubiertos:  " << tripsCubiertos << " / " << data.n << "\n"
             << "  Slacks activos:    " << slacks << "\n";
        if (slacks > 0 && slacks <= 60) {
            cout << "  IDs no cubiertos: ";
            for (int id : slackIds) cout << id << " ";
            cout << "\n";
        }
        cout << "  Costo total:       " << fixed << setprecision(2) << costo << "\n";
    }
};

// ============================================================================
// CALLBACK
// ============================================================================
class VSPSolutionCallback : public BcSolutionFoundCallback {
public:
    VSPData& data;
    MejorSolucion& mejor;
    mutable int solutionCount;

    VSPSolutionCallback(VSPData& d, MejorSolucion& m)
        : data(d), mejor(m), solutionCount(0) {}

    bool operator()(BcSolution newSolution) const override
    {
        double cost = newSolution.cost();
        solutionCount++;
        if (cost >= mejor.costo) return true;

        MejorSolucion nueva;
        nueva.costo = cost;

        set<int> tripsCovered;
        set<int> slacksActive;

        // ----------------------------------------------------------------
        // La solución es una cadena:
        //   - primera solución: variables MASTER puras (SlackNoCubierto)
        //   - siguientes: columnas de subproblema (tripVar, costVar)
        // getVar() extrae SOLO las variables de la solución actual,
        // a diferencia de extractVar() que recorre toda la cadena.
        // ----------------------------------------------------------------
        BcSolution col = newSolution;
        bool esMaster = true;

        while (col.defined()) {

            set<BcVar> varSet;
            col.getVar(varSet);  // <-- clave: solo esta columna

            if (esMaster) {
                // Primera solución: pure master vars (slacks)
                for (const BcVar& v : varSet) {
                    if (v.solVal() < 0.5) continue;
                    const string& name = v.name();
                    if (name.find("SlackNoCubierto") != string::npos) {
                        int id = -1;
                        sscanf(name.c_str(), "SlackNoCubierto_%d", &id);
                        if (id >= 0) slacksActive.insert(id);
                    }
                }
                esMaster = false;
                col = col.next();
                continue;
            }

            // Columna de subproblema
            int mult = col.getMultiplicity();
            if (mult < 1) { col = col.next(); continue; }

            vector<int> tripIds;
            double costaTM = 0.0;

            for (const BcVar& v : varSet) {
                if (v.solVal() < 0.5) continue;
                const string& name = v.name();
                if (name.find("tripVar") != string::npos) {
                    int id = -1;
                    sscanf(name.c_str(), "tripVar_%d", &id);
                    if (id >= 0) tripIds.push_back(id);
                } else if (name.find("costVar") != string::npos) {
                    costaTM = v.solVal();
                }
            }

            if (!tripIds.empty()) {
                sort(tripIds.begin(), tripIds.end(), [&](int a, int b){
                    return data.viajes[a].inicio < data.viajes[b].inicio;
                });

                MejorSolucion::Ruta r;
                r.trips   = tripIds;
                r.costaTM = costaTM;

                int dep = data.viajes[tripIds[0]].salida;
                if      (dep == 0) r.deposito = "Recreo";
                else if (dep == 1) r.deposito = "Lab   ";
                else if (dep == 2) r.deposito = "MV    ";
                else               r.deposito = "?     ";

                // mult > 1: la misma columna aparece varias veces en la
                // solución master (subproblemas idénticos)
                for (int m = 0; m < mult; ++m) {
                    nueva.rutas.push_back(r);
                    nueva.numRoutes++;
                    for (int tid : tripIds) tripsCovered.insert(tid);
                }
            }

            col = col.next();
        }

        nueva.tripsCubiertos = (int)tripsCovered.size();
        nueva.slacks         = (int)slacksActive.size();
        nueva.slackIds.assign(slacksActive.begin(), slacksActive.end());

        mejor = nueva;
        cout << "\n[Solución entera #" << solutionCount
             << " mejoró → costo = " << fixed << setprecision(2) << cost << "]\n";

        return true;
    }
};

// ============================================================================
// WARM START desde CSV con información de fase
// Formato CSV: ruta, viaje_id, fase, turno, is_single
// ============================================================================

struct RutaWS {
    int  deposito;    // 0=Recreo, 1=Lab, 2=MV 
    bool is_single;
    double tm;        // costVar = tiempo muerto calculado
    vector<int> trips; // índices en data.viajes, ordenados por inicio
};

// ============================================================================
// FUNCIONES DE PENALIZACIÓN (mismas que en VSPRCSPSolver.cpp)
// ============================================================================
static double penDescansoWS(double x) {
    if (x < 30.3)
        return 5.0 * (30.5 - x) * (30.5 - x);
    else
        return std::fabs(30.5 - x);
}

static double penIntraFaseWS(double t) {
    if (t >= 5.0 && t <= 10.0)  return 0.0;
    if (t < 2.0)                 return 45.0 - 15.0 * t;
    if (t < 5.0)                 return 5.0 * (5.0 - t);
    if (t <= 13.0)               return 5.0 * (t - 10.0);
    return 15.0 * t - 180.0;
}

// ============================================================================
// Penalización por duración de ruta — replica la lógica de los nodos puente.
// El RCSP escoge el arco más barato al que puede llegar dado su tm_total.
// Si no entra en ningún nivel → retorna el costo del nivel más ancho (nivel 3),
// porque el grafo garantiza que el nivel 3 cubre el rango factible completo.
// ============================================================================
static double penDuracionRutaWS(double tm_total, bool is_single,
                                 const VSPData& data) {
    const double  ideal   = is_single ? data.pen_dur_single_ideal   : data.pen_dur_full_ideal;
    const double* d_bajo  = is_single ? data.pen_dur_single_d       : data.pen_dur_full_d_bajo;
    const double* d_arr   = is_single ? data.pen_dur_single_d       : data.pen_dur_full_d_arriba;
    const double* c_bajo  = is_single ? data.pen_dur_single_c       : data.pen_dur_full_c_bajo;
    const double* c_arr   = is_single ? data.pen_dur_single_c       : data.pen_dur_full_c_arriba;

    bool por_abajo = (tm_total < ideal);
    for (int k = 0; k < 3; ++k) {
        double lb = ideal - d_bajo[k];
        double ub = ideal + d_arr[k];
        if (tm_total >= lb && tm_total <= ub)
            return por_abajo ? c_bajo[k] : c_arr[k];
    }
    return por_abajo ? c_bajo[2] : c_arr[2];
}

// Calcula costVar = tiempo_muerto + penalizaciones de descanso + penalizaciones
// intra-fase + penalización por duración total de ruta (nodos puente hacia SINK).
// NO penaliza el cambio de turno (el gap F1->F2 se ignora igual que en el grafo).
static double calcularTM(const vector<int>& trips, const VSPData& data,
                         bool is_single) {
    double tm = 0.0;
    for (int k = 0; k < (int)trips.size(); ++k) {
        const auto& v = data.viajes[trips[k]];
        int dur = v.fin - v.inicio;
        tm += dur;  // siempre suma duración del viaje

        if (k > 0) {
            const auto& vp = data.viajes[trips[k-1]];
            int gap = v.inicio - vp.fin;

            // Cambio de turno: NO se suma el gap (igual que el arco F1->F2 en el grafo)
            bool es_cam = (!is_single &&
                           gap > (int)data.max_espera_transicion &&
                           gap <= (int)data.max_espera_cam_turno);
            if (es_cam) continue;

            // Descanso: gap en [min_espera_transicion, max_espera_transicion]
            bool es_descanso = (gap >= (int)data.min_espera_transicion &&
                                gap <= (int)data.max_espera_transicion);

            tm += gap;

            if (es_descanso) {
                tm += penDescansoWS((double)gap);
            } else {
                tm += penIntraFaseWS((double)gap);
            }
        }
    }

    // ------------------------------------------------------------------
    // Penalización por duración total de ruta (nodos puente hacia SINK).
    // El arco F3(i)->puente o F1(i)->puente suma dur_ultimo en tiempo_muerto,
    // pero ese dur ya fue sumado al inicio del bucle (k=last).
    // Por tanto tm ya incluye la duración del último viaje: es el tm_total real.
    // ------------------------------------------------------------------
    tm += penDuracionRutaWS(tm, is_single, data);

    return tm;
}

static vector<RutaWS> parsearWarmStartCSV(const string& archivo,
                                           const VSPData& data) {
    vector<RutaWS> resultado;
    ifstream f(archivo);
    if (!f.is_open()) {
        cout << "  [warmstart] No se pudo abrir: " << archivo << endl;
        return resultado;
    }

    // Construir mapa viaje_id -> índice en data.viajes
    unordered_map<int,int> idAIdx;
    for (int j = 0; j < data.n; ++j)
        idAIdx[data.viajes[j].id] = j;

    // Leer encabezado
    string linea;
    getline(f, linea); // ruta,viaje_id,fase,turno,is_single

    // Agrupar por ruta
    map<string, vector<pair<int,int>>> por_ruta; // ruta -> [(viaje_idx, fase)]
    map<string, bool>   ruta_single;

    int lineas_ok = 0, lineas_err = 0;
    while (getline(f, linea)) {
        if (linea.empty()) continue;
        // Parse CSV: ruta,viaje_id,fase,turno,is_single
        istringstream ss(linea);
        string tok_ruta, tok_vid, tok_fase, tok_turno, tok_single;
        if (!getline(ss, tok_ruta,   ',')) { lineas_err++; continue; }
        if (!getline(ss, tok_vid,    ',')) { lineas_err++; continue; }
        if (!getline(ss, tok_fase,   ',')) { lineas_err++; continue; }
        if (!getline(ss, tok_turno,  ',')) { lineas_err++; continue; }
        if (!getline(ss, tok_single, ',')) { lineas_err++; continue; }

        int vid    = stoi(tok_vid);
        int fase   = stoi(tok_fase);
        bool single = (stoi(tok_single) == 1);

        auto it = idAIdx.find(vid);
        if (it == idAIdx.end()) { lineas_err++; continue; }

        int idx = it->second;
        por_ruta[tok_ruta].push_back({idx, fase});
        ruta_single[tok_ruta]   = single;
        lineas_ok++;
    }

    // Construir RutaWS por cada ruta
    for (auto& [nombre, viajes_fase] : por_ruta) {
        // Ordenar por tiempo de inicio
        sort(viajes_fase.begin(), viajes_fase.end(), [&](auto& a, auto& b){
            return data.viajes[a.first].inicio < data.viajes[b.first].inicio;
        });

        RutaWS r;
        r.is_single = ruta_single[nombre];
        for (auto& [idx, fase] : viajes_fase)
            r.trips.push_back(idx);

        // Depósito desde primer viaje
        r.deposito = data.viajes[r.trips[0]].salida;

        // Calcular tiempo muerto (incluye penalización por duración)
        r.tm = calcularTM(r.trips, data, r.is_single);

        resultado.push_back(r);
    }

    cout << "  [warmstart] Rutas parseadas: " << resultado.size()
         << "  Filas CSV ok: " << lineas_ok
         << "  Errores: " << lineas_err << endl;
    return resultado;
}

static void aplicarWarmStart(BcMaster& master,
                              BcColGenSpArray& colGenSp,
                              const vector<RutaWS>& rutas,
                              const VSPData& data) {
    if (rutas.empty()) return;

    vector<unique_ptr<BcSolution>> columnas;
    int col_ok = 0, col_skip = 0;

    for (const auto& r : rutas) {
        if (r.deposito < 0 || r.deposito >= data.d) { col_skip++; continue; }
        if (r.trips.empty())                         { col_skip++; continue; }

        BcFormulation sp = colGenSp[r.deposito];
        BcVarArray tripVars(sp, "tripVar");
        BcVarArray costVar(sp, "costVar");
        BcVarArray singleShiftVar(sp, "singleShiftVar");

        auto col = make_unique<BcSolution>(sp);

        // Añadir tripVars
        for (int idx : r.trips) {
            BcVar v = tripVars[idx];
            v = 1.0;
            *col += v;
        }

        // costVar: tiempo muerto + penalización por duración de ruta
        {
            BcVar cv = costVar[0];
            cv = r.tm;
            *col += cv;
        }

        // singleShiftVar: 1 si es single-shift, 0 si es doble turno
        // SIEMPRE se incluye para que BaPCod pueda evaluar la restricción
        {
            BcVar ssv = singleShiftVar[0];
            ssv = r.is_single ? 1.0 : 0.0;
            *col += ssv;
        }

        columnas.push_back(std::move(col));
        col_ok++;
    }

    cout << "  [warmstart] Columnas construidas: " << col_ok
         << "  Saltadas: " << col_skip << endl;

    if (columnas.empty()) return;

    // Encadenar columnas: col[0]->col[1]->...->col[n-1]
    for (size_t i = 0; i + 1 < columnas.size(); ++i)
        columnas[i]->appendSol(*columnas[i+1]);

    // Diagnóstico de cobertura
    set<int> tripsCubiertos;
    for (const auto& col_ptr : columnas) {
        BcSolution tmp = *col_ptr;
        set<BcVar> vs;
        tmp.extractVar("tripVar", vs);
        for (const BcVar& v : vs) {
            if (v.solVal() > 0.5) {
                int id = -1;
                sscanf(v.name().c_str(), "tripVar_%d", &id);
                if (id >= 0) tripsCubiertos.insert(id);
            }
        }
    }
    cout << "  [warmstart] Viajes cubiertos: "
         << tripsCubiertos.size() << " / " << data.n << endl;

    if ((int)tripsCubiertos.size() < data.n) {
        cout << "  [warmstart] Viajes NO cubiertos: ";
        int cnt = 0;
        for (int j = 0; j < data.n && cnt < 20; ++j)
            if (tripsCubiertos.find(j) == tripsCubiertos.end()) {
                cout << j << " "; cnt++;
            }
        cout << endl;
    }

    BcVarArray slackVars(master, "SlackNoCubierto");
    BcSolution masterSol(master);
    for (int i = 0; i < data.n; ++i) {
        BcVar sv = slackVars[i];
        sv = 0.0;
        masterSol += sv;
    }
    masterSol.appendSol(*columnas[0]);
    master.initializeWithSolution(masterSol);
    cout << "  [warmstart] initializeWithSolution llamado con "
         << col_ok << " columnas." << endl;
}

// ============================================================================
// MAIN
// ============================================================================
int main(int argc, char *argv[]) {

    BcInitialisation bapcodInit(argc, argv);

    VSPData data;

    string app_config = "config/appParams.cfg";
    for (int i = 1; i < argc - 1; ++i)
        if (string(argv[i]) == "-a") app_config = argv[i+1];

    data.leerConfigApp(app_config);

    for (int i = 1; i < argc; ++i) {
        if (string(argv[i]) == "-i" && i+1 < argc) {
            string f = argv[i+1];
            string ext = f.rfind('.') != string::npos ? f.substr(f.rfind('.')) : "";
            if (ext == ".txt") data.file_viajes = f;
            else { data.file_terminales = f; if (i+2 < argc) data.file_viajes = argv[i+2]; }
            break;
        }
    }

    try {
        cout << "\nCargando datos..." << endl;
        string ext = data.file_viajes.rfind('.') != string::npos
                   ? data.file_viajes.substr(data.file_viajes.rfind('.')) : "";
        if (ext == ".txt") data.leerInstanciaTxt(data.file_viajes);
        else                data.leerInstancia(data.file_terminales, data.file_viajes);
        cout << "Datos cargados exitosamente" << endl;
    }
    catch (const exception& e) { cerr << "\nERROR: " << e.what() << endl; return 1; }

    BcModel vspModel(bapcodInit, "VSP_Model_RCSP");
    BcColGenSpArray colGenSp(vspModel);
    BcMaster master(vspModel);
    buildVSPModel(data, vspModel, colGenSp, master);

    // Warm start desde CSV
    if (!data.file_warmstart.empty()) {
        cout << "\n  Cargando warm start desde: " << data.file_warmstart << endl;
        auto rutas = parsearWarmStartCSV(data.file_warmstart, data);
        aplicarWarmStart(master, colGenSp, rutas, data);
    }

    MejorSolucion mejorSolucion;
    VSPSolutionCallback* cb = new VSPSolutionCallback(data, mejorSolucion);
    vspModel.attach(cb);
    cout << "\nCallback de solución entera registrado.\n";

    cout << "\n--- INICIANDO BRANCH-AND-PRICE CON RCSP ---\n" << endl;

    try {
        BcSolution solution = vspModel.solve();
        cout << "\n--- SOLUCION FINAL (LP del ultimo nodo) ---\n" << endl;
        cout << solution << endl;
    }
    catch (const exception& e) { cerr << "\nERROR: " << e.what() << endl; return 1; }

    if (mejorSolucion.costo < 1e17) {
        cout << "  MEJOR SOLUCIÓN ENTERA AL FINAL DEL B&B\n";
        mejorSolucion.imprimir(data);
    } else {
        cout << "\nNo se encontró ninguna solución entera.\n";
    }

    cout << "\n--- FIN  ---\n" << endl;
    return 0;
}
