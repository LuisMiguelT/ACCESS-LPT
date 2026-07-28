#ifndef VSPDATA_HPP
#define VSPDATA_HPP
 
#include <iostream>
#include <vector>
#include <list>
#include <string>
#include <sstream>
#include <fstream>
#include <unordered_map>
#include <map>
#include <algorithm>
#include <limits>
#include <cmath>
 
using namespace std;
 
// === CLASES  ===
 
class Terminal {
public:
    int id;
    string nombre;
    int num_buses;
    Terminal(int i, string n, int b) : id(i), nombre(n), num_buses(b) {}
};
 
class Viaje {
public:
    int id, salida, llegada, inicio, fin;
    string circuito;
    Viaje(int id, int s, int l, int i, int f, string c)
        : id(id), salida(s), llegada(l), inicio(i), fin(f), circuito(c) {}
    bool operator<(const Viaje& otro) const {
        if (inicio != otro.inicio) return inicio < otro.inicio;
        return id < otro.id;
    }
};
 
 
class VSPData {
public:
    // --------------------------------------------------------
    // Contadores
    // --------------------------------------------------------
    int n;  // número de viajes
    int d;  // número de depósitos/terminales
 
    // --------------------------------------------------------
    // Archivos de datos
    // --------------------------------------------------------
    string file_terminales = "data/Quitumbe_terminales.csv";
    string file_viajes     = "data/Quitumbe_Viajes.csv";
    string file_warmstart  = "";   // CSV de warm start (vacío = sin warm start)
 
    // --------------------------------------------------------
    // Jornada laboral
    // --------------------------------------------------------
    double jornada         = 960.0;
    double ds              = 30.0;
    double cp              = 20.0;

    double max_espera      =120;// Para la función calibración
    double min_espera      =0; 
    // --------------------------------------------------------
    // Costos (se recalculan en calibración)
    // --------------------------------------------------------
    double costo_fijo_ruta          = 1000.0;
    double costo_columna_artificial = 10000.0;
    double tv_min_promedio          = 45.0;
 
    // --------------------------------------------------------
    // Grafo multi-capa: flexibilidad y fases
    // --------------------------------------------------------
 
    double fase_0_min =   0.0;
    double fase_0_max = 235.0;
    double fase_1_min = 245.0;
    double fase_1_max = 570.0;
    double fase_2_min = 490.0;
    double fase_2_max = 735.0;
    double fase_3_min = 745.0;
    double fase_3_max = 1080.0;
 
    double max_tiempo_ruta = 1080.0;
 
    // --------------------------------------------------------
    // Esperas entre viajes
    // --------------------------------------------------------
    double min_espera_intra_fase  = 3.0;
    double max_espera_intra_fase  = 120.0;
    double min_espera_transicion  = 3.0;
    double max_espera_transicion  = 70.0;
    double min_espera_cam_turno   = 7.0;
    double max_espera_cam_turno   = 140.0;
 
    // --------------------------------------------------------
    // Penalizaciones ideales por transición entre fases
    // --------------------------------------------------------
    double pen_ideal_f0_f1 = 30.0;
    double pen_ideal_f2_f3 = 30.0;
 
    // --------------------------------------------------------
    // Penalización intra-fase por espera fuera del rango ideal
    // --------------------------------------------------------
    double pen_intra_t_lineal_low  =  3.0;
    double pen_intra_t_ideal_low   =  5.0;
    double pen_intra_t_ideal_high  = 10.0;
    double pen_intra_m1            = 10.0;
    double pen_intra_m2            =  2.5;
    double pen_intra_k             =  0.05;
 
    // --------------------------------------------------------
    // Single-shift (rutas de un solo turno)
    // --------------------------------------------------------
    int    max_rutas_single_shift          = 2;
    double costo_penalizacion_single_shift = 0.0; // 0 => se calcula como cfr/2
 
    double pen_dur_full_ideal   = 910.0;
    double pen_dur_full_d_bajo[3]  = {10.0,  15.0,  25.0};  // LB = ideal - d_bajo
    double pen_dur_full_d_arriba[3]= {20.0,  50.0,  65.0};  // UB = ideal + d_arriba
    double pen_dur_full_c_bajo[3]  = {20.0,  80.0, 200.0};  // costo si ruta < ideal
    double pen_dur_full_c_arriba[3]= {10.0,  40.0, 100.0};  // costo si ruta > ideal
 
    double pen_dur_single_ideal = 455.0;
    double pen_dur_single_d[3]  = {10.0,  25.0,  55.0};
    double pen_dur_single_c[3]  = {10.0,  40.0, 100.0};
 
    // Calculado automáticamente en calibrarCostos(), NO editar a mano
    double dur_viaje_minima = 0.0;
 
    // --------------------------------------------------------
    // Poda de nodos por ventana horaria de salida (en minutos)
    // --------------------------------------------------------
    double poda_f01_sal_max = -1.0;
    double poda_f23_sal_min = -1.0;
 
    // --------------------------------------------------------
    // Colecciones
    // --------------------------------------------------------
    vector<Terminal> terminales;
    vector<Viaje>    viajes;
 
    VSPData() : n(0), d(0) {}
 
    // ============================================================
    // leerConfigApp: carga TODOS los parámetros desde el .cfg
    // ============================================================
    void leerConfigApp(const string& archivo) {
        ifstream file(archivo);
        if (!file.is_open()) {
            cout << " No se pudo abrir " << archivo
                 << ", usando valores por defecto." << endl;
            return;
        }
 
        string linea;
        while (getline(file, linea)) {
            auto comentario = linea.find('#');
            if (comentario != string::npos)
                linea = linea.substr(0, comentario);
 
            istringstream ss(linea);
            string clave, igual, valor;
            if (!(ss >> clave >> igual >> valor)) continue;
            if (igual != "=") continue;
 
            // --- Archivos ---
            if      (clave == "file_terminales")          file_terminales          = valor;
            else if (clave == "file_viajes")              file_viajes              = valor;
            else if (clave == "file_warmstart")           file_warmstart           = valor;
 
            // --- Jornada ---
            else if (clave == "jornada")                  jornada                  = stod(valor);
            else if (clave == "ds")                       ds                       = stod(valor);
            else if (clave == "cp")                       cp                       = stod(valor);
 
 
            // --- Costos ---
            else if (clave == "costo_fijo_ruta")          costo_fijo_ruta          = stod(valor);
            else if (clave == "costo_columna_artificial") costo_columna_artificial = stod(valor);
            else if (clave == "tv_min_promedio")          tv_min_promedio          = stod(valor);
 
            // --- Grafo: límites de fase ---
            else if (clave == "fase_0_min")               fase_0_min               = stod(valor);
            else if (clave == "fase_0_max")               fase_0_max               = stod(valor);
            else if (clave == "fase_1_min")               fase_1_min               = stod(valor);
            else if (clave == "fase_1_max")               fase_1_max               = stod(valor);
            else if (clave == "fase_2_min")               fase_2_min               = stod(valor);
            else if (clave == "fase_2_max")               fase_2_max               = stod(valor);
            else if (clave == "fase_3_min")               fase_3_min               = stod(valor);
            else if (clave == "fase_3_max")               fase_3_max               = stod(valor);
            else if (clave == "max_tiempo_ruta")          max_tiempo_ruta          = stod(valor);
 
            // --- Esperas ---
            else if (clave == "min_espera_intra_fase")    min_espera_intra_fase    = stod(valor);
            else if (clave == "max_espera_intra_fase")    max_espera_intra_fase    = stod(valor);
            else if (clave == "min_espera_transicion")    min_espera_transicion    = stod(valor);
            else if (clave == "max_espera_transicion")    max_espera_transicion    = stod(valor);
            else if (clave == "min_espera_cam_turno")     min_espera_cam_turno     = stod(valor);
            else if (clave == "max_espera_cam_turno")     max_espera_cam_turno     = stod(valor);
 
 
            // --- Penalizaciones de transición entre fases ---
            else if (clave == "pen_ideal_f0_f1")          pen_ideal_f0_f1          = stod(valor);
            else if (clave == "pen_ideal_f2_f3")          pen_ideal_f2_f3          = stod(valor);
 
            // --- Penalización intra-fase por tramos ---
            else if (clave == "pen_intra_t_lineal_low")   pen_intra_t_lineal_low   = stod(valor);
            else if (clave == "pen_intra_t_ideal_low")    pen_intra_t_ideal_low    = stod(valor);
            else if (clave == "pen_intra_t_ideal_high")   pen_intra_t_ideal_high   = stod(valor);
            else if (clave == "pen_intra_m1")             pen_intra_m1             = stod(valor);
            else if (clave == "pen_intra_m2")             pen_intra_m2             = stod(valor);
            else if (clave == "pen_intra_k")              pen_intra_k              = stod(valor);
 
            // --- Single-shift ---
            else if (clave == "max_rutas_single_shift")           max_rutas_single_shift           = stoi(valor);
            else if (clave == "costo_penalizacion_single_shift")  costo_penalizacion_single_shift  = stod(valor);
 
            // --- Penalización duración ruta completa (3 niveles, tiempo_muerto) ---
            else if (clave == "pen_dur_full_ideal")    pen_dur_full_ideal    = stod(valor);
            else if (clave == "pen_dur_full_d1")        pen_dur_full_d_bajo[0] = pen_dur_full_d_arriba[0] = stod(valor);
            else if (clave == "pen_dur_full_d2")        pen_dur_full_d_bajo[1] = pen_dur_full_d_arriba[1] = stod(valor);
            else if (clave == "pen_dur_full_d3")        pen_dur_full_d_bajo[2] = pen_dur_full_d_arriba[2] = stod(valor);
            else if (clave == "pen_dur_full_d1_bajo")   pen_dur_full_d_bajo[0]  = stod(valor);
            else if (clave == "pen_dur_full_d2_bajo")   pen_dur_full_d_bajo[1]  = stod(valor);
            else if (clave == "pen_dur_full_d3_bajo")   pen_dur_full_d_bajo[2]  = stod(valor);
            else if (clave == "pen_dur_full_d1_arriba") pen_dur_full_d_arriba[0]= stod(valor);
            else if (clave == "pen_dur_full_d2_arriba") pen_dur_full_d_arriba[1]= stod(valor);
            else if (clave == "pen_dur_full_d3_arriba") pen_dur_full_d_arriba[2]= stod(valor);
            else if (clave == "pen_dur_full_c1")       { pen_dur_full_c_bajo[0] = pen_dur_full_c_arriba[0] = stod(valor); }
            else if (clave == "pen_dur_full_c2")       { pen_dur_full_c_bajo[1] = pen_dur_full_c_arriba[1] = stod(valor); }
            else if (clave == "pen_dur_full_c3")       { pen_dur_full_c_bajo[2] = pen_dur_full_c_arriba[2] = stod(valor); }
            else if (clave == "pen_dur_full_c1_bajo")   pen_dur_full_c_bajo[0]   = stod(valor);
            else if (clave == "pen_dur_full_c2_bajo")   pen_dur_full_c_bajo[1]   = stod(valor);
            else if (clave == "pen_dur_full_c3_bajo")   pen_dur_full_c_bajo[2]   = stod(valor);
            else if (clave == "pen_dur_full_c1_arriba") pen_dur_full_c_arriba[0] = stod(valor);
            else if (clave == "pen_dur_full_c2_arriba") pen_dur_full_c_arriba[1] = stod(valor);
            else if (clave == "pen_dur_full_c3_arriba") pen_dur_full_c_arriba[2] = stod(valor);
            else if (clave == "pen_dur_single_ideal")  pen_dur_single_ideal  = stod(valor);
            else if (clave == "pen_dur_single_d1")     pen_dur_single_d[0]   = stod(valor);
            else if (clave == "pen_dur_single_d2")     pen_dur_single_d[1]   = stod(valor);
            else if (clave == "pen_dur_single_d3")     pen_dur_single_d[2]   = stod(valor);
            else if (clave == "pen_dur_single_c1")     pen_dur_single_c[0]   = stod(valor);
            else if (clave == "pen_dur_single_c2")     pen_dur_single_c[1]   = stod(valor);
            else if (clave == "pen_dur_single_c3")     pen_dur_single_c[2]   = stod(valor);
 
            // --- Poda de nodos por ventana horaria ---
            else if (clave == "poda_f01_sal_max")         poda_f01_sal_max         = stod(valor);
            else if (clave == "poda_f23_sal_min")         poda_f23_sal_min         = stod(valor);
 
            else {
                cout << " [cfg] Parámetro desconocido ignorado: " << clave << endl;
            }
        }
 
        cout << " Configuración app cargada desde: " << archivo << endl;
    }
 
    // ============================================================
    // Conversión de hora "HH:MM" a minutos
    // ============================================================
    int horaTextoAMinutos(const string& hora_str) {
        int horas = 0, minutos = 0;
        char sep;
        stringstream ss(hora_str);
        ss >> horas >> sep >> minutos;
        return horas * 60 + minutos;
    }
 
    // ============================================================
    // Carga completa desde CSVs separados
    // ============================================================
    void leerInstancia(string archivo_terminales, string archivo_viajes) {
        leerTerminales(archivo_terminales);
        leerViajes(archivo_viajes);
        calibrarCostos();
    }
 
    // ============================================================
    // Carga desde formato de instancia .txt
    // ============================================================
    void leerInstanciaTxt(const string& archivo) {
        ifstream file(archivo);
        if (!file.is_open())
            throw runtime_error("No se pudo abrir instancia: " + archivo);
 
        terminales.clear();
        viajes.clear();
        n = 0;
        d = 0;
 
        string linea;
        string seccion = "";
        int header_d = 0, header_n = 0;
 
        while (getline(file, linea)) {
            if (linea.empty()) continue;
            if (linea[0] == '#') {
                seccion = linea;
                continue;
            }
 
            istringstream ss(linea);
 
            if (seccion.find("Instancia") != string::npos) {
                int tercero = 0;
                ss >> header_d >> header_n >> tercero;
                cout << "  [Instancia] terminales=" << header_d
                     << "  viajes=" << header_n
                     << "  (tercer valor=" << tercero << " ignorado)" << endl;
            }
            else if (seccion.find("ep") != string::npos) {
                int id, buses;
                string nombre;
                if (!(ss >> id >> nombre >> buses)) continue;
                terminales.push_back(Terminal(id, nombre, buses));
            }
            else if (seccion.find("Viajes") != string::npos) {
                int id, dep_sal, dep_lleg;
                string hora_ini, hora_fin;
                if (!(ss >> id >> hora_ini >> hora_fin >> dep_sal >> dep_lleg)) continue;
                int ini = horaTextoAMinutos(hora_ini);
                int fin = horaTextoAMinutos(hora_fin);
                viajes.push_back(Viaje(id, dep_sal, dep_lleg, ini, fin, ""));
            }
        }
 
        sort(viajes.begin(), viajes.end());
        d = (int)terminales.size();
        n = (int)viajes.size();
 
        if (header_d > 0 && d != header_d)
            cout << "  [ADVERTENCIA] Encabezado indica " << header_d
                 << " terminales pero se leyeron " << d << endl;
        if (header_n > 0 && n != header_n)
            cout << "  [ADVERTENCIA] Encabezado indica " << header_n
                 << " viajes pero se leyeron " << n << endl;
 
        cout << "Cargadas " << d << " terminales." << endl;
        cout << "Cargados " << n << " viajes." << endl;
 
        calibrarCostos();
    }
 
    void leerTerminales(const string& archivo) {
        ifstream file(archivo);
        if (!file.is_open())
            throw runtime_error("No se pudo abrir terminales: " + archivo);
        string linea, valor;
        getline(file, linea);
        while (getline(file, linea)) {
            stringstream ss(linea);
            getline(ss, valor, ','); int id    = stoi(valor);
            getline(ss, valor, ','); string nombre = valor;
            getline(ss, valor, ','); int buses = stoi(valor);
            terminales.push_back(Terminal(id, nombre, buses));
        }
        d = terminales.size();
        cout << "Cargadas " << d << " terminales." << endl;
    }
 
    void leerViajes(const string& archivo) {
        ifstream file(archivo);
        if (!file.is_open())
            throw runtime_error("No se pudo abrir viajes: " + archivo);
        string linea, valor;
        getline(file, linea);
        while (getline(file, linea)) {
            stringstream ss(linea);
            getline(ss, valor, ','); int id   = stoi(valor);
            getline(ss, valor, ','); int sal  = stoi(valor);
            getline(ss, valor, ','); int lleg = stoi(valor);
            getline(ss, valor, ','); int ini  = horaTextoAMinutos(valor);
            getline(ss, valor, ','); int fin  = horaTextoAMinutos(valor);
            getline(ss, valor, ','); string circ = valor;
            viajes.push_back(Viaje(id, sal, lleg, ini, fin, circ));
        }
        sort(viajes.begin(), viajes.end());
        n = viajes.size();
        cout << "Cargados " << n << " viajes." << endl;
    }
 
    // ============================================================
    // Calibración de costos
    // ============================================================
    void calibrarCostos() {
        cout << "\n=== CALIBRACIÓN DE COSTOS ===" << endl;
 
        double suma_costos = 0.0;
        int    num_conexiones = 0;
        for (int i = 0; i < n; i++)
            for (int j = 0; j < n; j++) {
                if (i == j) continue;
                if (viajes[i].llegada == viajes[j].salida) {
                    double espera = (double)(viajes[j].inicio - viajes[i].fin);
                    if (espera >= min_espera && espera <= max_espera) {
                        suma_costos += espera;
                        num_conexiones++;
                    }
                }
            }
        double cap = (num_conexiones > 0) ? (suma_costos / num_conexiones) : 30.0;
        cout << "   cap (costo promedio conexión): " << cap << endl;
        cout << "   num_conexiones analizadas: "     << num_conexiones << endl;
 
        map<pair<int,int>, pair<double,int>> duraciones;
        for (const auto& v : viajes) {
            double dur = (double)(v.fin - v.inicio);
            duraciones[{v.salida, v.llegada}].first  += dur;
            duraciones[{v.salida, v.llegada}].second += 1;
        }
        double min_tv = numeric_limits<double>::max();
        bool   hay    = false;
        for (const auto& p : duraciones) {
            double tv = p.second.first / p.second.second;
            if (tv > 0.0 && tv < min_tv) { min_tv = tv; hay = true; }
        }
        if (!hay) {
            cout << "   NO se pudo calcular tv_min_promedio, "
                 << "se mantiene el valor del cfg (" << tv_min_promedio << ")" << endl;
        } else {
            tv_min_promedio = min_tv;
        }
        cout << "   tv_min_promedio: " << tv_min_promedio << endl;
 
        dur_viaje_minima = numeric_limits<double>::max();
        for (const auto& v : viajes) {
            double dur = (double)(v.fin - v.inicio);
            if (dur > 0.0 && dur < dur_viaje_minima)
                dur_viaje_minima = dur;
        }
        if (dur_viaje_minima == numeric_limits<double>::max())
            dur_viaje_minima = 0.0;
        cout << "   dur_viaje_minima: " << dur_viaje_minima << endl;
 
        double num_viajes_jornada = (jornada - 2.0 * ds - cp) / tv_min_promedio;
        costo_fijo_ruta          = cap * (ceil(num_viajes_jornada) - 1.0) + (2.0 * ds + cp);
        costo_columna_artificial = 15 * costo_fijo_ruta;
 
        if (costo_penalizacion_single_shift <= 0.0) {
            costo_penalizacion_single_shift = costo_fijo_ruta / 2.0;
        }
 
        cout << "   num_viajes_jornada: " << ceil(num_viajes_jornada) << endl;
        cout << "   costo_fijo_ruta:    " << costo_fijo_ruta          << endl;
        cout << "   costo_pen_single:   " << costo_penalizacion_single_shift << endl;
        cout << "   max_rutas_single:   " << max_rutas_single_shift   << endl;
 
        // Mostrar ventanas de penalización por duración
        cout << "   pen_dur_full:   ideal=" << pen_dur_full_ideal << endl;
        for (int k = 0; k < 3; ++k)
            cout << "     nivel " << (k+1) << ": ["
                 << (pen_dur_full_ideal - pen_dur_full_d_bajo[k]) << ", "
                 << (pen_dur_full_ideal + pen_dur_full_d_arriba[k]) << "]"
                 << "  c_bajo=" << pen_dur_full_c_bajo[k]
                 << "  c_arriba=" << pen_dur_full_c_arriba[k] << endl;
        cout << "   pen_dur_single: ideal=" << pen_dur_single_ideal << endl;
        for (int k = 0; k < 3; ++k)
            cout << "     nivel " << (k+1) << ": ["
                 << (pen_dur_single_ideal - pen_dur_single_d[k]) << ", "
                 << (pen_dur_single_ideal + pen_dur_single_d[k]) << "]"
                 << "  costo=" << pen_dur_single_c[k] << endl;
 
        cout << "=== FIN CALIBRACIÓN ===\n" << endl;
 
        if (poda_f01_sal_max < 0)
            poda_f01_sal_max = 9 * 60 + 480;   // 9:00 + 8h = 17:00 = 1020 min
 
        if (!viajes.empty()) {
            double primer_inicio = (double)viajes[0].inicio;
            for (const auto& v : viajes)
                if ((double)v.inicio < primer_inicio)
                    primer_inicio = (double)v.inicio;
 
            if (poda_f23_sal_min < 0)
                poda_f23_sal_min = primer_inicio + 420.0;
 
            int h_max = (int)poda_f01_sal_max / 60;
            int m_max = (int)poda_f01_sal_max % 60;
            int h_min = (int)poda_f23_sal_min / 60;
            int m_min = (int)poda_f23_sal_min % 60;
            cout << "   poda F0/F1 sal <= "
                 << h_max << ":" << (m_max < 10 ? "0" : "") << m_max << endl;
            cout << "   poda F2/F3 sal >= "
                 << h_min << ":" << (m_min < 10 ? "0" : "") << m_min << endl;
        }
    }
};
 
#endif // VSPDATA_HPP
