#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import tkinter as tk
from tkinter import ttk, filedialog
import sqlite3
import customtkinter as ctk
import matplotlib.pyplot as plt
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
import threading
import time
import json
import numpy as np
import ast
import re
import base64
import struct
from collections import deque
from typing import Optional
from websocket import WebSocketApp

# Optional online learning dependencies
try:
    from sklearn.linear_model import SGDClassifier, PassiveAggressiveClassifier
    from sklearn.preprocessing import StandardScaler
    SKLEARN_AVAILABLE = True
except Exception:
    SKLEARN_AVAILABLE = False

# ============================================================
# GLOBAL APPLICATION STATE (mutable, shared across threads)
# ============================================================

class AppState:
    ESP_IP: str
    WS_URL: str
    ws: Optional[WebSocketApp]
    running: bool
    paused: bool


state = AppState()

# --- WebSocket & data flow ---
state.ESP_IP = "192.168.4.1" #"192.168.100.232"#"192.168.4.1"
state.WS_URL = f"ws://{state.ESP_IP}:81"
state.ws = None
state.running = False
state.paused = False

# Data storage
state.raw_data = [[] for _ in range(8)]
state.averaged_data = [[] for _ in range(8)]
state.timestamps = []
state.anomalies = []
state.db_path = "events_database.sqlite"
state.db_conn = None
state.db_lock = threading.RLock()

# ML features (optional)
# state.ml_features = {
#     'ldc1': {feat: [[] for _ in range(4)] for feat in ['mean', 'stdDev', 'gradient', 'anomalyScore', 'status', 'minVal', 'maxVal', 'lastValue']},
#     'ldc2': {feat: [[] for _ in range(4)] for feat in ['mean', 'stdDev', 'gradient', 'anomalyScore', 'status', 'minVal', 'maxVal', 'lastValue']}
# }

state.ml_features = {
    'ldc1': {feat: [[] for _ in range(4)] for feat in []},
    'ldc2': {feat: [[] for _ in range(4)] for feat in []}
}

# Parameters sent to ESP
state.parameters = {
    "L": [13.0] * 8,
    "C": [390.0] * 8,
    "R": [1.5] * 8,
    "delay": 10,
}
state.channel_params = {
    "MIN": [13.0] * 8,
    "MAX": [390.0] * 8,
}

# Detection parameters (threshold‑based)
state.detection_threshold_percent = 3.0      # % deviation from baseline
state.detection_min_duration = 10            # minimum samples
state.detection_exit_hysteresis = 5          # consecutive below‑threshold to end
state.detection_slope_threshold = 1.0        # min change per sample to consider an edge
state.detection_min_prominence = 0.8         # additional amplitude prominence to accept event
state.detection_min_event_amplitude = 600.0  # absolute minimum event amplitude to count as event

# GUI related
state.anomaly_labels = {}                    # label -> color
state.current_anomaly_label = "Vehicle"
state.display_window_size = 500              # number of samples to show
state.plot_show_raw = True
state.plot_show_events = True

state.view_offset = 0                        # starting sample index of the current view
state.is_live_view = True                    # if True, automatically follows the latest data

state.detector = None                        # will hold the event detector instance
state.channel_active = [True] * 8            # channels can be deactivated if all-zero at start
state.detectors = [None] * 8                 # per-channel detectors
state.channel_baselines = [None] * 8         # fixed baseline per channel, refreshed during calibration
state.detection_mode = "HYBRID"              # EMBEDDED, RAW, HYBRID
state.latest_status = {}
state.latest_config = {}
state.latest_cpu_stats = {}
state.latest_speed = {}
state.latest_recalibrate = None
state.last_status_timestamp = 0.0
state.heartbeat_interval = 20.0
state.command_queue = deque()
state.command_lock = threading.Lock()

# Online learning state
state.online_learning_method = "SGD"
state.online_model = None
state.online_scaler = None
state.online_classes = []
state.online_training_samples = []
state.online_training_labels = []

# Calibration state
state.calibrating = False                    # whether calibration is running
state.calibration_duration = 5               # seconds to collect for calibration by default
state.calibration_samples = 300              # alternative: number of samples per channel to collect
state.calibration_buffer = [[] for _ in range(8)]  # temporary collection during calibration
state.channel_noise_rms = [None] * 8        # computed per-channel noise RMS from calibration

# ============================================================
# HELPER FUNCTIONS
# ============================================================

def queue_command(command):
    """Queue a WebSocket command for sequential delivery."""
    with state.command_lock:
        if command not in state.command_queue:
            state.command_queue.append(command)
            #print(f"Queued: {command}")
        else:
            #print(f"Already queued: {command}")
            pass


def send_command(command):
    """Send a command via the WebSocket queue."""
    queue_command(command)


def set_detection_mode(mode):
    """Set embedded detection mode on ESP32."""
    mode = str(mode).upper().strip()
    if mode not in ("EMBEDDED", "RAW", "HYBRID"):
        print(f"Invalid detection mode: {mode}")
        return
    state.detection_mode = mode
    send_command(f"SET_MODE|{mode}")
    print(f"Detection mode set to {mode}")


def send_parameter(param, index=None, value=None):
    """Send parameter to ESP via WebSocket or queue if disconnected."""
    if index is not None:
        msg = f"{param}{index},{value}"
    else:
        msg = f"{param},{value}"
    send_command(msg)


def process_command_queue():
    """Worker thread that sends queued commands one at a time."""
    while True:
        next_cmd = None
        with state.command_lock:
            if state.command_queue:
                next_cmd = state.command_queue.popleft()
        if next_cmd is not None:
            if state.ws and state.ws.sock and state.ws.sock.connected:
                try:
                    state.ws.send(next_cmd)
                    #print(f"Sent queued command: {next_cmd}")
                except Exception as exc:
                    print(f"Queued send failed: {exc}")
                    with state.command_lock:
                        state.command_queue.appendleft(next_cmd)
                    time.sleep(0.5)
            else:
                with state.command_lock:
                    state.command_queue.appendleft(next_cmd)
                time.sleep(0.5)
        else:
            time.sleep(0.1)


def heartbeat_worker():
    """Periodic heartbeat and status polling."""
    while True:
        if time.time() - state.last_status_timestamp > state.heartbeat_interval * 2:
            print("Heartbeat: no STATUS response in >10s")
        if state.ws and state.ws.sock and state.ws.sock.connected:
            send_command("GET_STATUS")
        time.sleep(state.heartbeat_interval)


def _parse_kv_parts(parts):
    feats = {}
    for part in parts:
        if ':' in part:
            key, value = part.split(':', 1)
            key = key.strip().lower()
            value = value.strip()
            if key in ('sig', 'signature'):
                try:
                    feats['signature'] = [float(x.strip()) for x in value.split(',') if x.strip()]
                except ValueError:
                    try:
                        raw = base64.b64decode(value)
                        count = len(raw) // 4
                        feats['signature'] = list(struct.unpack(f'{count}f', raw[:count * 4]))
                    except Exception:
                        feats['signature'] = value
            elif key in ('class', 'type', 'label', 'channel_id'):
                feats[key] = value
            else:
                try:
                    feats[key] = float(value)
                except ValueError:
                    feats[key] = value
    return feats

def _first_present(mapping, *keys, default=None):
    for key in keys:
        if key in mapping and mapping[key] is not None:
            return mapping[key]
    return default

def _to_float(value, default=None):
    if value is None or value == '':
        return default
    try:
        return float(value)
    except (TypeError, ValueError):
        return default

def _to_int(value, default=None):
    if value is None or value == '':
        return default
    try:
        return int(round(float(value)))
    except (TypeError, ValueError):
        return default

def _signature_to_blob(signature):
    if not signature:
        return None
    if isinstance(signature, (bytes, bytearray)):
        return sqlite3.Binary(bytes(signature))
    if isinstance(signature, str):
        try:
            values = [float(x.strip()) for x in signature.split(',') if x.strip()]
        except ValueError:
            return signature.encode('utf-8')
    else:
        try:
            values = [float(x) for x in signature]
        except (TypeError, ValueError):
            return None
    return sqlite3.Binary(struct.pack(f'{len(values)}f', *values))


def _parse_channel_index(channel_id):
    if not channel_id:
        return 0
    match = re.search(r'(\d+)$', channel_id)
    if match:
        return int(match.group(1))
    match = re.search(r'CH(\d+)', channel_id, re.I)
    if match:
        return int(match.group(1))
    return 0


def handle_sensor_data(message):
    parts = message[12:].split("|", 1)
    if len(parts) < 2:
        return
    sensor_vals_str, ml_json_str = parts
    sensor_vals = list(map(int, sensor_vals_str.split(",")))
    if len(sensor_vals) != 8:
        return
    for i in range(8):
        state.raw_data[i].append(sensor_vals[i])
        if len(state.raw_data[i]) % 5 == 0:
            avg = np.mean(state.raw_data[i][-5:])
            state.averaged_data[i].append(avg)
            if i == 0:
                state.timestamps.append(time.time())
    try:
        ml = json.loads(ml_json_str)
        for sensor in ['ldc1', 'ldc2']:
            if sensor in ml:
                for feat in state.ml_features[sensor]:
                    if feat in ml[sensor]:
                        vals = ml[sensor][feat]
                        for ch in range(4):
                            if ch < len(vals):
                                state.ml_features[sensor][feat][ch].append(vals[ch])
    except Exception:
        pass


def handle_embedded_event(message):
    parts = message.split('|')
    if len(parts) < 2:
        return
    channel_id = parts[1]
    feats = _parse_kv_parts(parts[2:])
    if feats.get('channel_id'):
        channel_id = str(feats.get('channel_id'))
    ch = _parse_channel_index(channel_id)
    ch = max(0, min(ch, 7))
    current_idx = len(state.averaged_data[ch])
    duration_ms = _to_float(_first_present(feats, 'duration_ms', 'dur'), 0.0) or 0.0
    #duration_samples = int(round(duration_ms / 40.0)) if duration_ms > 0 else 1 #changed from /5 to /40 to match 25Hz sampling 
    duration_samples = int(round(duration_ms / 160.0)) if duration_ms > 0 else 1
    start_idx = max(0, current_idx - duration_samples)
    end_idx = max(start_idx, current_idx)
    overlap = any(not (end_idx < a['start_idx'] or start_idx > a['end_idx']) for a in state.anomalies)
    if overlap:
        print(f"Embedded EVENT skipped due overlapping interval: {message}")
        return
    event_label = str(_first_present(feats, 'label', 'class', 'type', default='Unknown'))
    event_features = {
        'channel_id': channel_id,
        'duration_ms': duration_ms,
        'peak_value': _first_present(feats, 'peak', 'peak_value', 'peak_dev'),
        'peak_dev': _first_present(feats, 'peak_dev', 'peak', 'peak_value'),
        'width_half_max': _first_present(feats, 'width_half_max', 'w50'),
        'rise_ms': _first_present(feats, 'rise_ms', 'rise'),
        'decay_ms': _first_present(feats, 'decay_ms', 'decay'),
        'area': feats.get('area'),
        'energy': feats.get('energy'),
        'event_energy': feats.get('energy'),
        'mean_dev': feats.get('mean'),
        'std_dev': feats.get('std'),
        'skew': feats.get('skew'),
        'kurt': feats.get('kurt'),
        'dft_re': feats.get('dft_re'),
        'dft_im': feats.get('dft_im'),
        'num_peaks': feats.get('num_peaks'),
        'max_slope': feats.get('max_slope'),
        'com_idx': feats.get('com_idx'),
        'anomaly_score': _first_present(feats, 'anomaly_score', 'score'),
        'signature': feats.get('signature'),
        'rise_time_samples': (_to_float(_first_present(feats, 'rise_ms', 'rise')) / 40.0)
                             if _first_present(feats, 'rise', 'rise') is not None else None,
        'decay_time_samples': (_to_float(_first_present(feats, 'decay_ms', 'decay')) / 40.0)
                              if _first_present(feats, 'decay_ms', 'decay') is not None else None,
        'baseline': feats.get('baseline'),
		'crest_factor': feats.get('crest'),
        'estimated_length': feats.get('est_len')
    }
    record = {
        'start_idx': start_idx,
        'end_idx': end_idx,
        'label': event_label,
        'channels': [ch],
        'auto_detected': True,
        'source': 'ESP32',
        'duration': duration_ms,
        'features': event_features
    }
    state.anomalies.append(record)
    state.anomaly_labels.setdefault(record['label'], 'red')
    db_id = insert_event_record(record)
    if db_id:
        record['db_id'] = db_id
    #print(f"Embedded EVENT recorded: {record}")
    print(f"Embedded EVENT recorded: {message}")
    refresh_stats_table()


def handle_speed(message):
    parts = message.split('|')
    if len(parts) < 4:
        return
    try:
        speed = float(parts[1])
    except ValueError:
        speed = None
    try:
        length_m = float(parts[2])
    except ValueError:
        length_m = None
    vehicle_type = parts[3] if len(parts) > 3 else ''
    state.latest_speed = {'speed': speed, 'length_m': length_m, 'type': vehicle_type, 'timestamp': time.time()}
    insert_speed_record(speed, length_m, vehicle_type)
    print(f"SPEED update: {state.latest_speed}")
    refresh_stats_table()


def handle_status(message):
    parts = message.split('|')[1:]
    status = {}
    for part in parts:
        if ':' in part:
            key, value = part.split(':', 1)
            status[key] = value
    state.latest_status = status
    state.last_status_timestamp = time.time()
    #print(f"STATUS received: {status}")


def handle_config_response(message):
    parts = message.split('|')[1:]
    config = {}
    for part in parts:
        if ':' in part:
            key, value = part.split(':', 1)
            config[key] = value
    state.latest_config = config
    print(f"CONFIG response: {config}")


def handle_cpu_stats(message):
    parts = message.split('|')[1:]
    cores = {}
    for idx, part in enumerate(parts):
        try:
            cores[f'core{idx}'] = float(part.strip('%'))
        except ValueError:
            cores[f'core{idx}'] = part
    state.latest_cpu_stats = cores
    print(f"CPU_ACK: {cores}")


def handle_recalibrate_alert(message):
    parts = message.split('|')
    channel = parts[1] if len(parts) > 1 else None
    info = parts[2] if len(parts) > 2 else ''
    state.latest_recalibrate = {'channel': channel, 'info': info, 'timestamp': time.time()}
    print(f"RECALIBRATE_NEEDED: {state.latest_recalibrate}")


def insert_speed_record(speed, length_m, vehicle_type):
    try:
        with state.db_lock:
            if state.db_conn is None:
                open_db()
            if state.db_conn is None:
                return False
            state.db_conn.execute('''
                CREATE TABLE IF NOT EXISTS speed_records (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    speed REAL,
                    length_m REAL,
                    type TEXT,
                    created_at TEXT DEFAULT CURRENT_TIMESTAMP
                )
            ''')
            state.db_conn.execute('''
                INSERT INTO speed_records (speed, length_m, type) VALUES (?, ?, ?)
            ''', (speed, length_m, vehicle_type))
            state.db_conn.commit()
            return True
    except Exception as exc:
        print(f"Speed record insert failed: {exc}")
        return False

def update_console(settings_window, message):
    """Show message in a settings window console."""
    if settings_window and hasattr(settings_window, 'console_text'):
        settings_window.console_text.insert(tk.END, message + "\n")
        settings_window.console_text.see(tk.END)
    print(message)

def clear_data():
    """Clear all stored data, anomalies."""
    for ch in range(8):
        state.raw_data[ch].clear()
        state.averaged_data[ch].clear()
    state.timestamps.clear()
    state.anomalies.clear()    
    # --- ADD THESE TWO LINES ---
    state.is_live_view = True
    state.view_offset = 0
    # Reset auto‑scale
    reset_zoom()
    print("All data cleared.")

def _flatten_anomaly_for_save(anom):
    row = {k: v for k, v in anom.items() if k != 'features'}
    for fk, fv in anom.get('features', {}).items():
        row[f'feature_{fk}'] = fv
    return row


VEHICLE_CLASSES = [
    "Motor", "Car", "Pickup", "Van", "Bus",
    "Truck-S", "Truck-2", "Truck-3", "Truck-4+", "Unknown",
]


def _normalize_label(label):
    if label is None:
        return "Unknown"
    s = str(label).strip()
    if not s:
        return "Unknown"
    su = s.upper()
    alias = {
        "MOTOR": "Motor", "MOTORCYCLE": "Motor", "MOTORBIKE": "Motor",
        "CAR": "Car",
        "PICKUP": "Pickup", "PICK-UP": "Pickup",
        "VAN": "Van",
        "BUS": "Bus",
        "TRUCK": "Truck-S", "TRUCKS": "Truck-S", "TRUCK-S": "Truck-S", "TRUCK_S": "Truck-S",
        "TRUCK-2": "Truck-2", "TRUCK2": "Truck-2", "TRUCK_2": "Truck-2",
        "TRUCK-3": "Truck-3", "TRUCK3": "Truck-3", "TRUCK_3": "Truck-3",
        "TRUCK-4": "Truck-4+", "TRUCK-4+": "Truck-4+", "TRUCK4": "Truck-4+", "TRUCK4+": "Truck-4+",
        "TRUCK_4": "Truck-4+", "TRUCK_4_PLUS": "Truck-4+", "TRUCK-4-PLUS": "Truck-4+",
        "UNKNOWN": "Unknown", "NOISE": "Unknown",
    }
    return alias.get(su, s if s in VEHICLE_CLASSES else "Unknown")


def _compute_live_stats():
    """Aggregate vehicle counts from state.anomalies and speed_records table."""
    counts = {cls: 0 for cls in VEHICLE_CLASSES}
    total = 0
    speeds = []
    for anom in state.anomalies:
        cls = _normalize_label(anom.get('label'))
        counts[cls] = counts.get(cls, 0) + 1
        total += 1
        try:
            f = anom.get('features', {}) or {}
            sp = f.get('speed_kmh')
            if sp is None:
                sp = f.get('speed')
            if sp is not None:
                speeds.append(float(sp))
        except Exception:
            pass

    # Aggregate from SQLite speed_records if connected
    if state.db_conn is not None:
        try:
            cur = state.db_conn.execute("SELECT type, speed FROM speed_records")
            for typ, sp in cur.fetchall():
                cls = _normalize_label(typ)
                counts[cls] = counts.get(cls, 0) + 1
                total += 1
                try:
                    if sp is not None:
                        speeds.append(float(sp))
                except Exception:
                    pass
        except Exception:
            pass

    avg_speed = (sum(speeds) / len(speeds)) if speeds else 0.0
    return counts, total, avg_speed


def refresh_stats_table():
    """Repopulate the live stats Treeview."""
    try:
        tree = globals().get('stats_tree')
        if tree is None:
            return
        for item in tree.get_children():
            tree.delete(item)
        counts, total, avg_speed = _compute_live_stats()
        tree.insert('', 'end', values=("— Total vehicles —", total))
        tree.insert('', 'end', values=("Average speed", f"{avg_speed:.1f} km/h"))
        for cls in VEHICLE_CLASSES:
            tree.insert('', 'end', values=(cls, counts.get(cls, 0)))
    except Exception as exc:
        print(f"refresh_stats_table failed: {exc}")


def periodic_stats_refresh():
    try:
        refresh_stats_table()
    except Exception:
        pass
    try:
        from tkinter import _default_root
        root_ref = _default_root
        if root_ref is not None:
            globals()['stats_after_id'] = root_ref.after(1500, periodic_stats_refresh)
    except Exception:
        pass

def ensure_db_schema(conn):
    conn.execute('''
        CREATE TABLE IF NOT EXISTS events (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,
            channel_id TEXT,
            label TEXT,
            start_idx INTEGER,
            end_idx INTEGER,
            duration REAL,
            duration_ms REAL,
            auto_detected INTEGER,
            channel INTEGER,
            baseline REAL,
            peak_value REAL,
            peak_dev REAL,
            event_energy REAL,
            event_power REAL,
            rise_time_samples REAL,
            rise_ms REAL,
            decay_time_samples REAL,
            decay_ms REAL,
            width_half_max REAL,
            area REAL,
            energy REAL,
            mean_dev REAL,
            std_dev REAL,
            dominant_frequency REAL,
            skewness REAL,
            kurtosis REAL,
            dft_re REAL,
            dft_im REAL,
            num_peaks INTEGER,
            max_slope REAL,
            com_idx REAL,
            anomaly_score INTEGER,
            crest_factor REAL,
            derivative_threshold REAL,
            num_samples INTEGER,
            estimated_length REAL,
            signature BLOB,
            source TEXT,
            created_at TEXT DEFAULT CURRENT_TIMESTAMP
        )
    ''')
    conn.commit()
    existing_columns = [row[1] for row in conn.execute("PRAGMA table_info(events)").fetchall()]
    migrations = {
        'timestamp': 'ALTER TABLE events ADD COLUMN timestamp DATETIME',
        'channel_id': 'ALTER TABLE events ADD COLUMN channel_id TEXT',
        'duration_ms': 'ALTER TABLE events ADD COLUMN duration_ms REAL',
        'peak_dev': 'ALTER TABLE events ADD COLUMN peak_dev REAL',
        'rise_ms': 'ALTER TABLE events ADD COLUMN rise_ms REAL',
        'decay_ms': 'ALTER TABLE events ADD COLUMN decay_ms REAL',
        'area': 'ALTER TABLE events ADD COLUMN area REAL',
        'energy': 'ALTER TABLE events ADD COLUMN energy REAL',
        'mean_dev': 'ALTER TABLE events ADD COLUMN mean_dev REAL',
        'std_dev': 'ALTER TABLE events ADD COLUMN std_dev REAL',
        'dft_re': 'ALTER TABLE events ADD COLUMN dft_re REAL',
        'dft_im': 'ALTER TABLE events ADD COLUMN dft_im REAL',
        'num_peaks': 'ALTER TABLE events ADD COLUMN num_peaks INTEGER',
        'com_idx': 'ALTER TABLE events ADD COLUMN com_idx REAL',
        'anomaly_score': 'ALTER TABLE events ADD COLUMN anomaly_score INTEGER',
        'signature': 'ALTER TABLE events ADD COLUMN signature BLOB',
        'source': 'ALTER TABLE events ADD COLUMN source TEXT',
        'estimated_length': 'ALTER TABLE events ADD COLUMN estimated_length REAL',
    }
    for column, sql in migrations.items():
        if column not in existing_columns:
            conn.execute(sql)
    conn.commit()

def open_db(path=None):
    try:
        with state.db_lock:
            if path is None:
                path = state.db_path
            conn = sqlite3.connect(path, check_same_thread=False)
            conn.execute("PRAGMA busy_timeout = 5000")
            ensure_db_schema(conn)
            state.db_conn = conn
            state.db_path = path
            print(f"Database opened: {path}")
            return conn
    except Exception as exc:
        print(f"DB open failed: {exc}")
        return None

def close_db():
    try:
        with state.db_lock:
            if state.db_conn is not None:
                state.db_conn.close()
                state.db_conn = None
                print("Database closed")
    except Exception as exc:
        print(f"DB close failed: {exc}")




def insert_event_record(anom):
    try:
        with state.db_lock:
            if state.db_conn is None:
                open_db()
            if state.db_conn is None:
                return False
            feats = anom.get('features', {}) or {}
            ch = (anom.get('channels') or [0])[0]
            row = {
                'timestamp': time.strftime('%Y-%m-%d %H:%M:%S'),
                'channel_id': feats.get('channel_id') or f"CH{int(ch)+1}",
                'label': anom.get('label', 'Unknown'),
                'start_idx': int(anom.get('start_idx', 0)),
                'end_idx': int(anom.get('end_idx', 0)),
                'duration': _to_float(anom.get('duration'), 0.0),
                'duration_ms': _to_float(feats.get('duration_ms'), _to_float(anom.get('duration'), None)),
                'auto_detected': 1 if anom.get('auto_detected') else 0, 
                'channel': int(ch),
                'baseline': _to_float(feats.get('baseline')),
                'peak_value': _to_float(feats.get('peak_value')),
                'peak_dev': _to_float(feats.get('peak_dev')),
                'event_energy': _to_float(feats.get('event_energy')),
                'event_power': _to_float(feats.get('event_power')),
                'rise_time_samples': _to_float(feats.get('rise_time_samples')),
                'rise_ms': _to_float(feats.get('rise_ms')),
                'decay_time_samples': _to_float(feats.get('decay_time_samples')),
                'decay_ms': _to_float(feats.get('decay_ms')),
                'width_half_max': _to_int(feats.get('width_half_max')),
                'area': _to_float(feats.get('area')),
                'energy': _to_float(feats.get('energy')),
                'mean_dev': _to_float(feats.get('mean_dev')),
                'std_dev': _to_float(feats.get('std_dev')),
                'dominant_frequency': _to_float(feats.get('dominant_frequency')),
                'decay_time_samples': _to_float(feats.get('decay')),
                'skewness': _to_float(feats.get('skew')),
                'kurtosis': _to_float(feats.get('kurt')),
                'dft_re': _to_float(feats.get('dft_re')),
                'dft_im': _to_float(feats.get('dft_im')),
                'num_peaks': _to_int(feats.get('num_peaks')),
                'max_slope': _to_float(feats.get('max_slope')),
                'com_idx': _to_float(feats.get('com_idx')),
                'anomaly_score': _to_int(feats.get('anomaly_score')),
                'crest_factor': _to_float(feats.get('crest_factor')),
                'derivative_threshold': _to_float(feats.get('derivative_threshold')),
                'num_samples': _to_int(feats.get('num_samples'), 0),
                'estimated_length': _to_float(feats.get('estimated_length')),
                'signature': _signature_to_blob(feats.get('signature')),
                'source': anom.get('source', 'Python')
            }
            columns = [
                'timestamp', 'channel_id', 'label', 'start_idx', 'end_idx',
                'duration', 'duration_ms', 'auto_detected', 'channel',
                'baseline', 'peak_value', 'peak_dev', 'event_energy', 'event_power',
                'rise_time_samples', 'rise_ms', 'decay_time_samples', 'decay_ms',
                'width_half_max', 'area', 'energy', 'mean_dev', 'std_dev',
                'dominant_frequency',
                'skewness', 'kurtosis', 'dft_re', 'dft_im', 'num_peaks',
                'max_slope', 'com_idx', 'anomaly_score', 'crest_factor',
                'derivative_threshold', 'num_samples', 'estimated_length', 'signature', 'source'
            ]
            placeholders = ', '.join('?' for _ in columns)
            cursor = state.db_conn.execute(
                f"INSERT INTO events ({', '.join(columns)}) VALUES ({placeholders})",
                tuple(row[k] for k in columns)
            )
            state.db_conn.commit()
            return cursor.lastrowid
    except Exception as exc:
        print(f"DB insert failed: {exc}")
        return False

def update_event_label_record(anom, old_start=None, old_end=None):
    try:
        if state.db_conn is None:
            open_db()
        if state.db_conn is None:
            return False
        event_id = anom.get('db_id')
        if event_id:
            state.db_conn.execute(
                'UPDATE events SET label=?, start_idx=?, end_idx=? WHERE id=?',
                (anom.get('label', 'Unknown'), int(anom.get('start_idx', 0)), int(anom.get('end_idx', 0)), int(event_id))
            )
        else:
            old_start = anom.get('start_idx') if old_start is None else old_start
            old_end = anom.get('end_idx') if old_end is None else old_end
            ch = (anom.get('channels') or [0])[0]
            cursor = state.db_conn.execute(
                '''
                UPDATE events
                SET label=?, start_idx=?, end_idx=?
                WHERE start_idx=? AND end_idx=? AND channel=?
                ''',
                (
                    anom.get('label', 'Unknown'),
                    int(anom.get('start_idx', 0)),
                    int(anom.get('end_idx', 0)),
                    int(old_start),
                    int(old_end),
                    int(ch),
                )
            )
            if cursor.rowcount == 0:
                return False
        state.db_conn.commit()
        return True
    except Exception as exc:
        print(f"DB label update failed: {exc}")
        return False

def export_db_to_csv(path=None):
    try:
        import pandas as pd
        if state.db_conn is None:
            open_db()
        if state.db_conn is None:
            return None
        if path is None:
            path = f"events_export_{time.strftime('%Y%m%d_%H%M%S')}.csv"
        df = pd.read_sql_query('SELECT * FROM events ORDER BY id DESC', state.db_conn)
        df.to_csv(path, index=False)
        print(f"DB export saved to {path}")
        return path
    except Exception as exc:
        print(f"DB export failed: {exc}")
        return None

def db_summary():
    try:
        if state.db_conn is None:
            open_db()
        if state.db_conn is None:
            return None
        rows = state.db_conn.execute('SELECT label, COUNT(*) FROM events GROUP BY label ORDER BY COUNT(*) DESC').fetchall()
        total = state.db_conn.execute('SELECT COUNT(*) FROM events').fetchone()[0]
        return {'total_events': total, 'by_label': rows}
    except Exception as exc:
        print(f"DB summary failed: {exc}")
        return None

def initialize_db():
    """Create/open the default SQLite database at startup."""
    try:
        open_db(state.db_path)
    except Exception as exc:
        print(f"Startup DB init failed: {exc}")

def save_event_summary():
    """Export a separate event summary file with one row per detected event."""
    import pandas as pd
    timestamp_str = time.strftime("%Y%m%d_%H%M%S")
    summary_xlsx = f"events_summary_{timestamp_str}.xlsx"
    summary_csv = f"events_summary_{timestamp_str}.csv"

    flat_anoms = [_flatten_anomaly_for_save(a) for a in state.anomalies]
    summary_df = pd.DataFrame(flat_anoms)

    try:
        with pd.ExcelWriter(summary_xlsx, engine='openpyxl') as writer:
            summary_df.to_excel(writer, sheet_name='Events', index=False)
        summary_df.to_csv(summary_csv, index=False)
        print(f"Event summary saved to {summary_xlsx} and {summary_csv}")
    except Exception as exc:
        print(f"Error exporting event summary: {exc}")

def save_data():
    """Save raw, averaged, anomalies to Excel."""
    if len(state.raw_data[0]) == 0:
        print("No data to save.")
        return
    import pandas as pd
    timestamp_str = time.strftime("%Y%m%d_%H%M%S")
    filename = f"labeled_data_{timestamp_str}.xlsx"
    try:
        with pd.ExcelWriter(filename, engine='openpyxl') as writer:
            # Get consistent length for raw data
            raw_len = len(state.raw_data[0])
            
            # Raw data - pad timestamps to match raw_data length
            raw_df = pd.DataFrame({f"CH{i+1}": state.raw_data[i][:raw_len] for i in range(8)})
            timestamps_padded = (state.timestamps + [None] * raw_len)[:raw_len]
            raw_df['Timestamp'] = timestamps_padded
            raw_df.to_excel(writer, sheet_name='RawData', index=False)
            
            # Averaged data
            max_len = max((len(state.averaged_data[i]) for i in range(8)), default=0)
            avg_dict = {f"AVG_CH{i+1}": (state.averaged_data[i] + [None]*max_len)[:max_len] for i in range(8)}
            if max_len > 0:
                pd.DataFrame(avg_dict).to_excel(writer, sheet_name='AveragedData', index=False)
            
            # Anomalies
            if state.anomalies:
                flat_anoms = [_flatten_anomaly_for_save(a) for a in state.anomalies]
                pd.DataFrame(flat_anoms).to_excel(writer, sheet_name='Anomalies', index=False)

            # ML features
            for sensor in ['ldc1', 'ldc2']:
                for feat in state.ml_features[sensor]:
                    feat_data = {}
                    max_feat_len = 0
                    for ch in range(4):
                        col = f"{sensor}_{feat}_CH{ch+1}"
                        lst = state.ml_features[sensor][feat][ch]
                        max_feat_len = max(max_feat_len, len(lst))
                        feat_data[col] = lst
                    if feat_data:
                        for col in feat_data:
                            feat_data[col] = (feat_data[col] + [None]*max_feat_len)[:max_feat_len]
                        if max_feat_len > 0:
                            pd.DataFrame(feat_data).to_excel(writer, sheet_name=f"{sensor}_{feat}", index=False)
        print(f"Data saved to {filename}")
    except Exception as e:
        print(f"Error saving data: {e}")

def snapshot(fig):
    """Save current figure as PNG."""
    if fig:
        ts = time.strftime("%Y%m%d_%H%M%S")
        fig.savefig(f"snapshot_{ts}.png", dpi=150)
        print(f"Snapshot saved")

# ============================================================
# NEW: PLAYBACK & DB EDITING
# ============================================================

def load_events_from_db_to_state():
    """Matches labels from SQL DB to currently loaded data based on sample indices."""
    if state.db_conn is None:
        update_console(settings_window, "No database connected.")
        return
    try:
        cursor = state.db_conn.execute('SELECT id, label, start_idx, end_idx, channel FROM events ORDER BY start_idx ASC')
        rows = cursor.fetchall()
        state.anomalies.clear()
        for event_id, label, start, end, channel in rows:
            if start is None or end is None:
                continue
            state.anomalies.append({
                'db_id': int(event_id),
                'start_idx': int(start),
                'end_idx': int(end),
                'label': label,
                'channels': [int(channel or 0)],
                'auto_detected': False,
                'features': {}
            })
            state.anomaly_labels.setdefault(label, 'red')
        update_console(settings_window, f"Synced {len(rows)} events from DB to plot.")
        try:
            plot_data([ax_main], canvas, channel_checkboxes)
        except NameError:
            pass
    except Exception as e:
        print(f"Error loading events from DB: {e}")

def play_xlsx_log(root_ref):
    """Loads a saved .xlsx log into the viewer and pauses live ingestion."""
    path = filedialog.askopenfilename(filetypes=[('Excel files', '*.xlsx'), ('All files', '*.*')])
    if not path:
        return
    try:
        import pandas as pd
        clear_data()
        state.running = False  # Pause live ingestion to prevent mixing streams
        state.detectors = [None] * 8
        state.channel_baselines = [None] * 8
        if hasattr(detect_anomalies, 'last_idx_global'):
            detect_anomalies.last_idx_global = 0
            
        df_raw = pd.read_excel(path, sheet_name='RawData')
        for i in range(8):
            col = f"CH{i+1}"
            if col in df_raw.columns:
                state.raw_data[i] = df_raw[col].dropna().tolist()
        if 'Timestamp' in df_raw.columns:
            state.timestamps = df_raw['Timestamp'].dropna().tolist()
            
        try:
            df_avg = pd.read_excel(path, sheet_name='AveragedData')
            for i in range(8):
                col = f"AVG_CH{i+1}"
                if col in df_avg.columns:
                    state.averaged_data[i] = df_avg[col].dropna().tolist()
        except ValueError:
            for i in range(8):
                data = state.raw_data[i]
                avg = []
                for j in range(len(data)):
                    start_idx = max(0, j-4)
                    avg.append(float(np.mean(data[start_idx:j+1])))
                state.averaged_data[i] = avg

        try:
            df_anom = pd.read_excel(path, sheet_name='Anomalies')
            for _, row in df_anom.iterrows():
                anom = row.to_dict()
                features = {}
                for k, v in anom.items():
                    if k.startswith('feature_'):
                        features[k.replace('feature_', '')] = v
                anom['features'] = features
                
                if 'channels' in anom and isinstance(anom['channels'], str):
                    try: anom['channels'] = ast.literal_eval(anom['channels'])
                    except: anom['channels'] = [0]
                elif 'channels' not in anom:
                    anom['channels'] = [0]
                    
                anom['start_idx'] = int(anom.get('start_idx', 0))
                anom['end_idx'] = int(anom.get('end_idx', 0))
                state.anomalies.append(anom)
                state.anomaly_labels.setdefault(anom.get('label', 'Unknown'), 'red')
        except ValueError:
            pass

        update_console(settings_window, f"Loaded log: {path}")
        try:
            plot_data([ax_main], canvas, channel_checkboxes)
        except NameError:
            pass
        
    except Exception as e:
        print(f"Error loading xlsx: {e}")
        update_console(settings_window, f"Error loading xlsx: {e}")

def open_db_label_editor(root_ref):
    """Opens UI to view, edit, and delete labels stored in the SQL file."""
    if state.db_conn is None:
        update_console(settings_window, "No database connected. Please open or create a DB first.")
        return
        
    editor = ctk.CTkToplevel(root_ref)
    editor.title("Edit Database Labels")
    editor.geometry("1000x900")
    
    try:
        cursor = state.db_conn.execute('SELECT id, label, start_idx, end_idx, channel, duration FROM events ORDER BY id DESC')
        rows = cursor.fetchall()
    except Exception as e:
        update_console(settings_window, f"Failed to fetch events: {e}")
        return

    cols = ('ID', 'Label', 'Start Idx', 'End Idx', 'Start Time', 'End Time', 'Channel', 'Duration')
    tree = ttk.Treeview(editor, columns=cols, show='headings')
    for col in cols:
        tree.heading(col, text=col)
        tree.column(col, width=80 if col not in ('Label', 'Start Time', 'End Time') else 120)
        
    for row in rows:
        event_id, label, start, end, channel, duration = row
        start_time = state.timestamps[int(start)] if int(start) < len(state.timestamps) else "N/A"
        end_time = state.timestamps[int(end)] if int(end) < len(state.timestamps) else "N/A"
        
        if isinstance(start_time, (int, float)):
            try: start_time = time.strftime('%H:%M:%S', time.localtime(start_time))
            except: pass
        if isinstance(end_time, (int, float)):
            try: end_time = time.strftime('%H:%M:%S', time.localtime(end_time))
            except: pass
            
        tree.insert('', tk.END, values=(event_id, label, start, end, start_time, end_time, channel, duration))
        
    tree.pack(fill='both', expand=True, padx=10, pady=10)
    
    def on_edit():
        selected = tree.selection()
        if not selected:
            return
        item = tree.item(selected[0])
        event_id, label, start, end, start_time, end_time, channel, duration = item['values']
        
        dialog = ctk.CTkToplevel(editor)
        dialog.title(f"Edit Event {event_id}")
        dialog.geometry("350x400")
        
        ctk.CTkLabel(dialog, text=f"Time: {start_time} -> {end_time}").pack(pady=5)
        
        ctk.CTkLabel(dialog, text="Label:").pack(pady=5)
        label_entry = ctk.CTkEntry(dialog, width=200)
        label_entry.insert(0, str(label))
        label_entry.pack()
        
        ctk.CTkLabel(dialog, text="Start Index:").pack(pady=5)
        start_entry = ctk.CTkEntry(dialog, width=200)
        start_entry.insert(0, str(start))
        start_entry.pack()
        
        ctk.CTkLabel(dialog, text="End Index:").pack(pady=5)
        end_entry = ctk.CTkEntry(dialog, width=200)
        end_entry.insert(0, str(end))
        end_entry.pack()
        
        def save_changes():
            try:
                new_label = label_entry.get()
                new_start = int(start_entry.get())
                new_end = int(end_entry.get())
                state.db_conn.execute(
                    'UPDATE events SET label=?, start_idx=?, end_idx=? WHERE id=?',
                    (new_label, new_start, new_end, event_id)
                )
                state.db_conn.commit()
                
                tree.item(selected[0], values=(event_id, new_label, new_start, new_end, start_time, end_time, channel, duration))
                
                for anom in state.anomalies:
                    if anom.get('db_id') == int(event_id) or (anom.get('start_idx') == int(start) and anom.get('end_idx') == int(end)):
                        anom['db_id'] = int(event_id)
                        anom['label'] = new_label
                        anom['start_idx'] = new_start
                        anom['end_idx'] = new_end
                state.anomaly_labels.setdefault(new_label, 'red')
                try:
                    plot_data([ax_main], canvas, channel_checkboxes)
                except NameError:
                    pass
                dialog.destroy()
            except Exception as e:
                print(f"Error saving: {e}")
                
        ctk.CTkButton(dialog, text="Save", command=save_changes).pack(pady=15)

    def on_delete():
        selected = tree.selection()
        if not selected:
            return
        item = tree.item(selected[0])
        event_id, label, start, end, start_time, end_time, channel, duration = item['values']
        try:
            state.db_conn.execute('DELETE FROM events WHERE id=?', (event_id,))
            state.db_conn.commit()
            tree.delete(selected[0])
            state.anomalies = [a for a in state.anomalies if not (a.get('start_idx') == int(start) and a.get('end_idx') == int(end))]
            try:
                plot_data([ax_main], canvas, channel_checkboxes)
            except NameError:
                pass
        except Exception as e:
            print(f"Error deleting: {e}")

    btn_frame = ctk.CTkFrame(editor)
    btn_frame.pack(pady=10)
    ctk.CTkButton(btn_frame, text="Edit Selected", command=on_edit).pack(side='left', padx=10)
    ctk.CTkButton(btn_frame, text="Delete Selected", command=on_delete).pack(side='left', padx=10)

# -----------------
# Feature extraction and online learning helpers
# -----------------




# -----------------
# Calibration helpers
# -----------------


# ============================================================
# ANOMALY DETECTION
# ============================================================




# ============================================================
# PLOTTING
# ============================================================

def is_channel_connected(ch_idx, min_samples=10):
    if len(state.raw_data[ch_idx]) < min_samples:
        return True
    recent = state.raw_data[ch_idx][-100:]
    return any(v != 0 for v in recent)

_auto_scale = {'global_min': float('inf'), 'global_max': float('-inf')}

def plot_data(axes, canvas, channel_checkboxes):
    """Main plotting function (single main axis)."""
    ax_main = axes[0]
    ax_main.clear()

    visible_channels = [i for i, chk in enumerate(channel_checkboxes)
                        if chk.get() and is_channel_connected(i) and len(state.averaged_data[i]) > 0]
    all_vals = []
    for i in visible_channels:
        if state.plot_show_raw:
            ax_main.plot(state.averaged_data[i], label=f"CH{i+1}", linewidth=1)
            all_vals.extend(state.averaged_data[i])

    if all_vals:
        current_min, current_max = min(all_vals), max(all_vals)
        if abs(current_max - current_min) < 1e-3:
            pad = max(1.0, abs(current_max) * 0.05)
        else:
            pad = (current_max - current_min) * 0.1
        ymin, ymax = (current_min - pad, current_max + pad)
        ax_main.set_ylim(ymin, ymax)
        _auto_scale['last_min'] = ymin
        _auto_scale['last_max'] = ymax
    else:
        if _auto_scale.get('last_min') is not None and _auto_scale.get('last_max') is not None:
            ax_main.set_ylim(_auto_scale['last_min'], _auto_scale['last_max'])

    if state.plot_show_events:
        for anom in state.anomalies:
            color = state.anomaly_labels.get(anom['label'], 'red')
            ax_main.axvspan(anom['start_idx'], anom['end_idx'], alpha=0.2, color=color)
            mid = (anom['start_idx'] + anom['end_idx']) / 2
            ylim = ax_main.get_ylim()
            ax_main.text(mid, ylim[0] + 0.05 * (ylim[1] - ylim[0]), anom['label'],
                         fontsize=8, ha='center', bbox=dict(facecolor='white', alpha=0.7))

    if len(state.averaged_data[0]) > 0:
        xmin = max(0, len(state.averaged_data[0]) - state.display_window_size)
        xmax = len(state.averaged_data[0])
        ax_main.set_xlim(xmin, xmax)

    try:
        ax_main.legend(loc='upper left', fontsize=8)
    except Exception:
        pass
    ax_main.set_title('Inductive Loop Monitor')
    ax_main.set_ylabel('Sensor Value')
    ax_main.set_xlabel('Sample Index')
    ax_main.grid(True, alpha=0.3)
    canvas.draw()

def reset_zoom():
    _auto_scale['global_min'] = float('inf')
    _auto_scale['global_max'] = float('-inf')

# ============================================================
# WEBSOCKET CLIENT
# ============================================================

def on_ws_message(ws, message):
    try:
        if message.startswith("SENSOR_DATA|"):
            handle_sensor_data(message)
        elif message.startswith("EVENT|"):
            if state.detection_mode in ("EMBEDDED", "HYBRID"):
                handle_embedded_event(message)
            else:
                print(f"EVENT received but RAW mode active; ignoring embedded event.")
        elif message.startswith("SPEED|"):
            handle_speed(message)
        elif message.startswith("STATUS|"):
            handle_status(message)
        elif message.startswith("CONFIG|"):
            handle_config_response(message)
        elif message.startswith("CPU_ACK|"):
            handle_cpu_stats(message)
        elif message.startswith("RECALIBRATE_NEEDED|"):
            handle_recalibrate_alert(message)
        elif message.endswith("_ACK"):
            print(f"✓ Command confirmed: {message}")
        else:
            print(f"Other WS message: {message[:80]}")
        cap = getattr(state, 'last_ws_capture', None)
        if cap is not None:
            try:
                cap(message)
            except Exception:
                pass
    except Exception as e:
        print(f"WS parse error: {e}")

def on_ws_error(ws, error):
    print(f"WebSocket error: {error}")

def on_ws_close(ws, close_status_code, close_msg):
    print("WebSocket closed")

def on_ws_open(ws):
    print("WebSocket connected")
    state.last_status_timestamp = time.time()
    send_command("GET_STATUS")


def run_websocket():
    state.ws = WebSocketApp(state.WS_URL,
                            on_open=on_ws_open,
                            on_message=on_ws_message,
                            on_error=on_ws_error,
                            on_close=on_ws_close)
    state.ws.run_forever()

# ============================================================
# GUI WINDOWS
# ============================================================

settings_window = None
counter_settings_window = None
label_window = None
channel_checkboxes = []
manual_selection_start = None

def open_settings_window(root_ref):
    global settings_window
    if settings_window and settings_window.winfo_exists():
        settings_window.lift()
        return
    settings_window = ctk.CTkToplevel(root_ref)
    settings_window.title("Parameters")
    settings_window.geometry("500x700")

    def create_param_row(parent, label_text, param, index=None):
        frame = ctk.CTkFrame(parent)
        frame.pack(pady=5, fill='x')
        ctk.CTkLabel(frame, text=label_text, width=100).pack(side='left', padx=5)
        entry = ctk.CTkEntry(frame, width=100)
        if index is not None:
            entry.insert(0, str(state.parameters[param][index]))
        else:
            entry.insert(0, str(state.parameters[param]))
        entry.pack(side='left', padx=5)
        def submit():
            val = entry.get()
            try:
                if index is not None:
                    state.parameters[param][index] = float(val)
                    send_parameter(param, index, float(val))
                else:
                    state.parameters[param] = float(val)
                    if param == 'delay':
                        send_parameter('D1', value=float(val))
                update_console(settings_window, f"Set {label_text} = {val}")
            except:
                update_console(settings_window, f"Invalid {label_text}")
        ctk.CTkButton(frame, text="Set", command=submit, width=60).pack(side='left')

    for i in range(8):
        create_param_row(settings_window, f"L{i+1}", "L", i)
        create_param_row(settings_window, f"C{i+1}", "C", i)
        create_param_row(settings_window, f"R{i+1}", "R", i)
    create_param_row(settings_window, "Delay", "delay")

    # Detection mode control
    mode_frame = ctk.CTkFrame(settings_window)
    mode_frame.pack(pady=10, fill='x')
    ctk.CTkLabel(mode_frame, text="Detection Mode:", width=140).pack(side='left', padx=5)
    mode_var = tk.StringVar(value=state.detection_mode)
    mode_combo = ctk.CTkComboBox(mode_frame, values=["EMBEDDED", "RAW", "HYBRID"], variable=mode_var)
    mode_combo.pack(side='left', padx=5, fill='x', expand=True)
    def apply_detection_mode():
        set_detection_mode(mode_var.get())
        update_console(settings_window, f"Detection mode -> {state.detection_mode}")
    ctk.CTkButton(mode_frame, text="Apply", command=apply_detection_mode, width=80).pack(side='left', padx=5)

    # Command buttons
    cmd_frame = ctk.CTkFrame(settings_window)
    cmd_frame.pack(pady=5, fill='x')
    ctk.CTkButton(cmd_frame, text="CALIBRATE", command=lambda: send_command("CALIBRATE")).pack(side='left', padx=5, pady=5, expand=True, fill='x')
    ctk.CTkButton(cmd_frame, text="GET_STATUS", command=lambda: send_command("GET_STATUS")).pack(side='left', padx=5, pady=5, expand=True, fill='x')
    ctk.CTkButton(cmd_frame, text="GET_CPU", command=lambda: send_command("GET_CPU")).pack(side='left', padx=5, pady=5, expand=True, fill='x')
    ctk.CTkButton(cmd_frame, text="GET_CONFIG", command=lambda: send_command("GET_CONFIG")).pack(side='left', padx=5, pady=5, expand=True, fill='x')

    console_frame = ctk.CTkFrame(settings_window)
    console_frame.pack(pady=10, fill='both', expand=True)
    ctk.CTkLabel(console_frame, text="WebSocket Messages:").pack()
    console_text = tk.Text(console_frame, wrap=tk.WORD, height=10)
    console_text.pack(side='left', fill='both', expand=True)
    scrollbar = ttk.Scrollbar(console_frame, orient='vertical', command=console_text.yview)
    scrollbar.pack(side='right', fill='y')
    console_text.config(yscrollcommand=scrollbar.set)
    settings_window.console_text = console_text

def open_counter_settings_window(root_ref):
    global counter_settings_window
    if counter_settings_window and counter_settings_window.winfo_exists():
        counter_settings_window.lift()
        return
    counter_settings_window = ctk.CTkToplevel(root_ref)
    counter_settings_window.title("Channel Limits")
    counter_settings_window.geometry("400x600")

    def create_row(parent, label_text, param, index):
        frame = ctk.CTkFrame(parent)
        frame.pack(pady=5, fill='x')
        ctk.CTkLabel(frame, text=label_text, width=100).pack(side='left')
        entry = ctk.CTkEntry(frame, width=100)
        entry.insert(0, str(state.channel_params[param][index]))
        entry.pack(side='left')
        def submit():
            try:
                val = float(entry.get())
                state.channel_params[param][index] = val
                send_parameter(param, index, val)
                update_console(counter_settings_window, f"Set {param}{index+1} = {val}")
            except:
                update_console(counter_settings_window, f"Invalid {param}{index+1}")
        ctk.CTkButton(frame, text="Set", command=submit, width=60).pack(side='left')

    for i in range(8):
        create_row(counter_settings_window, f"MIN{i+1}", "MIN", i)
        create_row(counter_settings_window, f"MAX{i+1}", "MAX", i)

def open_label_settings(root_ref):
    global label_window
    if label_window and label_window.winfo_exists():
        label_window.lift()
        return
    label_window = ctk.CTkToplevel(root_ref)
    label_window.title("Anomaly Labels")
    label_window.geometry("450x600")

    list_frame = ctk.CTkFrame(label_window)
    list_frame.pack(fill='both', expand=True, padx=10, pady=5)

    def refresh_label_list():
        for w in list_frame.winfo_children():
            w.destroy()
        for lbl, col in state.anomaly_labels.items():
            row = ctk.CTkFrame(list_frame)
            row.pack(fill='x', pady=2)
            ctk.CTkLabel(row, text=lbl, width=100).pack(side='left')
            ctk.CTkLabel(row, text=col, width=80).pack(side='left')
            ctk.CTkButton(row, text="Delete", command=lambda l=lbl: [state.anomaly_labels.pop(l, None), refresh_label_list()], width=60).pack(side='right')

    add_frame = ctk.CTkFrame(label_window)
    add_frame.pack(pady=10, fill='x')
    ctk.CTkLabel(add_frame, text="New Label:").pack(side='left', padx=5)
    label_entry = ctk.CTkEntry(add_frame, width=120)
    label_entry.pack(side='left', padx=5)
    ctk.CTkLabel(add_frame, text="Color:").pack(side='left', padx=5)
    color_entry = ctk.CTkEntry(add_frame, width=80, placeholder_text="red, blue, #FF0000")
    color_entry.pack(side='left', padx=5)
    def add_label():
        lbl = label_entry.get().strip()
        col = color_entry.get().strip()
        if lbl and col:
            state.anomaly_labels[lbl] = col
            refresh_label_list()
            label_entry.delete(0, 'end')
            color_entry.delete(0, 'end')
    ctk.CTkButton(add_frame, text="Add", command=add_label, width=60).pack(side='left', padx=5)

    auto_frame = ctk.CTkFrame(label_window)
    auto_frame.pack(pady=10, fill='x')
    ctk.CTkLabel(auto_frame, text="Auto‑detection default label:").pack(side='left', padx=5)
    auto_entry = ctk.CTkEntry(auto_frame, width=120)
    auto_entry.insert(0, state.current_anomaly_label)
    auto_entry.pack(side='left', padx=5)
    def set_auto_label():
        state.current_anomaly_label = auto_entry.get().strip()
        update_console(label_window, f"Auto label set to {state.current_anomaly_label}")
    ctk.CTkButton(auto_frame, text="Set", command=set_auto_label, width=60).pack(side='left')

    param_frame = ctk.CTkFrame(label_window)
    param_frame.pack(pady=10, fill='x')
    ctk.CTkLabel(param_frame, text="Detection Parameters", font=("Arial", 12, "bold")).pack()
    def update_detection_params():
        state.detection_threshold_percent = threshold_slider.get()
        state.detection_min_duration = int(duration_slider.get())
        state.detection_exit_hysteresis = int(hysteresis_slider.get())
        state.detection_min_event_amplitude = float(min_amplitude_entry.get())
        update_console(label_window, f"Threshold={state.detection_threshold_percent:.2f}%, MinDur={state.detection_min_duration}, Hyst={state.detection_exit_hysteresis}, MinAmp={state.detection_min_event_amplitude}")

    ctk.CTkLabel(param_frame, text="Threshold (% of baseline):").pack()
    threshold_slider = ctk.CTkSlider(param_frame, from_=0.5, to=10.0, number_of_steps=95)
    threshold_slider.set(state.detection_threshold_percent)
    threshold_slider.pack(pady=5)
    ctk.CTkLabel(param_frame, text="Min duration (samples):").pack()
    duration_slider = ctk.CTkSlider(param_frame, from_=3, to=50, number_of_steps=47)
    duration_slider.set(state.detection_min_duration)
    duration_slider.pack(pady=5)
    ctk.CTkLabel(param_frame, text="Exit hysteresis (samples):").pack()
    hysteresis_slider = ctk.CTkSlider(param_frame, from_=1, to=20, number_of_steps=19)
    hysteresis_slider.set(state.detection_exit_hysteresis)
    hysteresis_slider.pack(pady=5)
    ctk.CTkLabel(param_frame, text="Min event amplitude:").pack(pady=(8,0))
    min_amplitude_entry = ctk.CTkEntry(param_frame, width=100)
    min_amplitude_entry.insert(0, str(state.detection_min_event_amplitude))
    min_amplitude_entry.pack(pady=5)

    ctk.CTkButton(param_frame, text="Apply", command=update_detection_params).pack(pady=10)

    refresh_label_list()
    if not state.anomaly_labels:
        state.anomaly_labels["Vehicle"] = "red"
        state.anomaly_labels["Noise"] = "orange"
        state.anomaly_labels["Unknown"] = "purple"
        refresh_label_list()

def find_anomaly_at_index(idx):
    for anom in state.anomalies:
        if anom['start_idx'] <= idx <= anom['end_idx']:
            return anom
    return None

def open_anomaly_label_dialog(root_ref, start, end, initial_label=None):
    dialog = ctk.CTkToplevel(root_ref)
    dialog.title("Label Interval")
    dialog.geometry("480x360")
    dialog.resizable(True, True)

    ctk.CTkLabel(dialog, text=f"Interval: {start} → {end}", font=(None, 12, "bold")).pack(pady=(12, 8))

    label_values = list(state.anomaly_labels.keys())
    if not label_values:
        label_values = ["Vehicle", "Noise", "Unknown"]

    label_var = tk.StringVar(value=initial_label or label_values[0])
    ctk.CTkLabel(dialog, text="Select or enter class label:").pack(anchor='w', padx=15)
    label_combo = ctk.CTkComboBox(dialog, values=label_values, variable=label_var)
    label_combo.pack(fill='x', padx=15, pady=5)

    custom_label = ctk.CTkEntry(dialog, placeholder_text="Custom label")
    custom_label.pack(fill='x', padx=15, pady=(0, 10))

    frame = ctk.CTkFrame(dialog)
    frame.pack(fill='x', padx=15, pady=5)
    ctk.CTkLabel(frame, text="Start index:").grid(row=0, column=0, sticky='w')
    start_entry = ctk.CTkEntry(frame, width=80)
    start_entry.grid(row=0, column=1, sticky='e', padx=5)
    start_entry.insert(0, str(start))

    ctk.CTkLabel(frame, text="End index:").grid(row=1, column=0, sticky='w', pady=(8,0))
    end_entry = ctk.CTkEntry(frame, width=80)
    end_entry.grid(row=1, column=1, sticky='e', padx=5, pady=(8,0))
    end_entry.insert(0, str(end))

    result = {'label': None, 'start': start, 'end': end}

    def on_save():
        label_text = custom_label.get().strip() or label_var.get().strip()
        if not label_text:
            return
        try:
            s = int(start_entry.get())
            e = int(end_entry.get())
        except ValueError:
            return
        if e < s:
            s, e = e, s
        result['label'] = label_text
        result['start'] = s
        result['end'] = e
        dialog.destroy()

    def on_cancel():
        result['label'] = None
        dialog.destroy()

    btn_frame = ctk.CTkFrame(dialog)
    btn_frame.pack(fill='x', pady=15, padx=15)
    ctk.CTkButton(btn_frame, text="Save", command=on_save).pack(side='left', expand=True, padx=5)
    ctk.CTkButton(btn_frame, text="Cancel", command=on_cancel).pack(side='right', expand=True, padx=5)

    dialog.grab_set()
    dialog.wait_window()
    return result if result['label'] else None

def create_main_window():
    global channel_checkboxes, fig, ax_main, canvas, manual_selection_start
    global stats_tree, stats_after_id
    ctk.set_appearance_mode("dark")
    ctk.set_default_color_theme("blue")
    root = ctk.CTk()
    root.title("Inductive Loop Monitor - Event Detection")
    root.geometry("1400x900")

    # --- MENU BAR ADDITION ---
    menubar = tk.Menu(root)
    root.config(menu=menubar)

    file_menu = tk.Menu(menubar, tearoff=0)
    menubar.add_cascade(label="File", menu=file_menu)
    file_menu.add_command(label="Play Saved Log (.xlsx)", command=lambda: play_xlsx_log(root))
    file_menu.add_separator()
    file_menu.add_command(label="Exit", command=root.quit)

    db_menu = tk.Menu(menubar, tearoff=0)
    menubar.add_cascade(label="Database", menu=db_menu)
    db_menu.add_command(label="View & Edit DB Labels", command=lambda: open_db_label_editor(root))
    db_menu.add_command(label="Sync DB Labels to Plot", command=load_events_from_db_to_state)
    # -------------------------

    paned = ttk.PanedWindow(root, orient=tk.HORIZONTAL)
    paned.pack(fill=tk.BOTH, expand=True)

    left_frame = ctk.CTkFrame(paned)
    paned.add(left_frame, weight=3)
    fig, ax_main = plt.subplots(1, 1, figsize=(10, 4))
    fig.tight_layout(pad=3.0)
    canvas = FigureCanvasTkAgg(fig, master=left_frame)
    canvas.get_tk_widget().pack(fill=tk.BOTH, expand=True)

    # ----- Live statistics table (below the plot) -----
    stats_frame = ctk.CTkFrame(left_frame)
    stats_frame.pack(fill='x', padx=4, pady=(4, 6))
    ctk.CTkLabel(stats_frame, text="📊 Live Vehicle Report", font=("Arial", 13, "bold")).pack(anchor='w', padx=6, pady=(4, 2))

    stats_cols = ('Metric', 'Value')
    stats_tree = ttk.Treeview(stats_frame, columns=stats_cols, show='headings', height=12)
    stats_tree.heading('Metric', text='Metric')
    stats_tree.heading('Value', text='Value')
    stats_tree.column('Metric', width=220, anchor='w')
    stats_tree.column('Value', width=120, anchor='center')
    stats_tree.pack(side='left', fill='both', expand=True, padx=4, pady=4)
    stats_scroll = ttk.Scrollbar(stats_frame, orient='vertical', command=stats_tree.yview)
    stats_scroll.pack(side='right', fill='y')
    stats_tree.configure(yscrollcommand=stats_scroll.set)
    refresh_stats_table()
    # ---------------------------------------------------

    right_frame = ctk.CTkFrame(paned)
    paned.add(right_frame, weight=1)

    right_frame.pack_propagate(False)
    right_canvas = tk.Canvas(right_frame, highlightthickness=0)
    right_scroll = ttk.Scrollbar(right_frame, orient='vertical', command=right_canvas.yview)
    right_scroll.pack(side='right', fill='y')
    right_canvas.configure(yscrollcommand=right_scroll.set)
    right_canvas.pack(side='left', fill='both', expand=True)
    right_inner = ctk.CTkFrame(right_canvas)
    frame_id = right_canvas.create_window((0, 0), window=right_inner, anchor='nw')

    def _on_right_frame_configure(event=None):
        right_canvas.configure(scrollregion=right_canvas.bbox('all'))
        try:
            width = max(1, right_frame.winfo_width() - 6)
            right_canvas.itemconfigure(frame_id, width=width)
        except Exception:
            pass

    right_frame.bind('<Configure>', _on_right_frame_configure)
    right_inner.bind('<Configure>', _on_right_frame_configure)

    def _on_mousewheel(event):
        right_canvas.yview_scroll(int(-1 * (event.delta / 120)), 'units')

    right_canvas.bind_all('<MouseWheel>', _on_mousewheel)
    right_canvas.bind_all('<Shift-MouseWheel>', lambda e: right_canvas.yview_scroll(int(-1 * (e.delta / 120)), 'units'))

    ctk.CTkLabel(right_inner, text="Visible Channels", font=("Arial", 14, "bold")).pack(pady=5)
    ch_frame = ctk.CTkFrame(right_inner)
    ch_frame.pack(pady=5, fill='x')
    channel_checkboxes = []

    def on_channel_toggle():
        for i, chk in enumerate(channel_checkboxes):
            state.channel_active[i] = bool(chk.get())
        plot_data([ax_main], canvas, channel_checkboxes)

    for i in range(8):
        var = ctk.CTkCheckBox(ch_frame, text=f"CH{i+1}", command=on_channel_toggle)
        var.pack(side='left', padx=2)
        if i < 4:
            var.select()
        channel_checkboxes.append(var)

    btn_frame = ctk.CTkFrame(right_inner)
    btn_frame.pack(pady=10, fill='x')
    def start():
        state.running = True
        state.paused = False
        state.detector = None   
        print("Started")
    def pause():
        state.paused = True
        print("Paused")
    def stop():
        state.running = False
        print("Stopped")
        save_data()
        snapshot(fig)
    ctk.CTkButton(btn_frame, text="Start", command=start).pack(side='left', padx=5)
    ctk.CTkButton(btn_frame, text="Pause", command=pause).pack(side='left', padx=5)
    ctk.CTkButton(btn_frame, text="Stop", command=stop).pack(side='left', padx=5)
    ctk.CTkButton(btn_frame, text="Clear", command=clear_data).pack(side='left', padx=5)

    def manual_label_start():
        def on_click(event):
            global manual_selection_start
            if event.inaxes != ax_main:
                return
            x = event.xdata
            if x is None or len(state.averaged_data[0]) == 0:
                return
            idx = int(round(x))
            idx = max(0, min(idx, len(state.averaged_data[0]) - 1))

            if manual_selection_start is None:
                manual_selection_start = idx
                ax_main.axvline(x=idx, color='green', linestyle='--', linewidth=1)
                canvas.draw()
                print(f"Start at index {idx}. Click end point.")
            else:
                start = min(manual_selection_start, idx)
                end = max(manual_selection_start, idx)
                manual_selection_start = None
                result = open_anomaly_label_dialog(root, start, end)
                if result:
                    state.anomaly_labels.setdefault(result['label'], 'red')
                    first_active = next((idx for idx, active in enumerate(state.channel_active) if active), 0)
                    features = extract_event_features(first_active, result['start'], result['end']) if any(state.channel_active) else {}
                    state.anomalies.append({
                        'start_idx': result['start'],
                        'end_idx': result['end'],
                        'label': result['label'],
                        'auto_detected': False,
                        'features': features
                    })
                    db_id = insert_event_record(state.anomalies[-1])
                    if db_id:
                        state.anomalies[-1]['db_id'] = db_id
                    if features:
                        feature_vector = [features[k] for k in sorted(features.keys())]
                        update_online_model(feature_vector, result['label'])
                    print(f"Manual anomaly: {result['label']} [{result['start']}:{result['end']}]")
                    plot_data([ax_main], canvas, channel_checkboxes)
                else:
                    print("Manual label cancelled.")
                if hasattr(fig, '_click_cid'):
                    fig.canvas.mpl_disconnect(fig._click_cid)

        if hasattr(fig, '_click_cid'):
            fig.canvas.mpl_disconnect(fig._click_cid)
        cid = fig.canvas.mpl_connect('button_press_event', on_click)
        fig._click_cid = cid
        print("Click two points on the plot to define anomaly region.")

    def edit_label_mode():
        def on_click(event):
            if event.inaxes != ax_main:
                return
            x = event.xdata
            if x is None or len(state.averaged_data[0]) == 0:
                return
            idx = int(round(x))
            anom = find_anomaly_at_index(idx)
            if anom is None:
                print("No anomaly at that location.")
                return
            result = open_anomaly_label_dialog(root, anom['start_idx'], anom['end_idx'], initial_label=anom.get('label'))
            if result:
                old_start = anom['start_idx']
                old_end = anom['end_idx']
                anom['start_idx'] = result['start']
                anom['end_idx'] = result['end']
                anom['label'] = result['label']
                state.anomaly_labels.setdefault(result['label'], 'red')
                update_event_label_record(anom, old_start=old_start, old_end=old_end)
                print(f"Updated anomaly: {result['label']} [{result['start']}:{result['end']}]")
                plot_data([ax_main], canvas, channel_checkboxes)
            else:
                print("Edit cancelled.")
            if hasattr(fig, '_click_cid'):
                fig.canvas.mpl_disconnect(fig._click_cid)

        if hasattr(fig, '_click_cid'):
            fig.canvas.mpl_disconnect(fig._click_cid)
        cid = fig.canvas.mpl_connect('button_press_event', on_click)
        fig._click_cid = cid
        print("Click an existing anomaly region to edit its interval or label.")

    ctk.CTkButton(right_inner, text="Label Interval (2 clicks)", command=manual_label_start).pack(pady=5, fill='x')
    ctk.CTkButton(right_inner, text="Edit Label (click anomaly)", command=edit_label_mode).pack(pady=5, fill='x')
    ctk.CTkButton(right_inner, text="Parameter Settings", command=lambda: open_settings_window(root)).pack(pady=5, fill='x')
    ctk.CTkButton(right_inner, text="Channel Limits", command=lambda: open_counter_settings_window(root)).pack(pady=5, fill='x')
    ctk.CTkButton(right_inner, text="Anomaly Labels", command=lambda: open_label_settings(root)).pack(pady=5, fill='x')
    ctk.CTkButton(right_inner, text="Save Data Now", command=save_data).pack(pady=5, fill='x')
    ctk.CTkButton(right_inner, text="Calibrate Noise (5s)", command=lambda: start_calibration(root, duration=5, samples=None)).pack(pady=5, fill='x')
    ctk.CTkButton(right_inner, text="Stop Calibration", command=lambda: stop_calibration(root)).pack(pady=5, fill='x')

    db_frame = ctk.CTkFrame(right_inner)
    db_frame.pack(pady=10, fill='x')
    ctk.CTkLabel(db_frame, text="Database Controls", font=("Arial", 13, "bold")).pack(pady=(0, 4))

    def create_new_db():
        path = filedialog.asksaveasfilename(defaultextension='.sqlite', filetypes=[('SQLite DB', '*.sqlite'), ('All files', '*.*')])
        if path:
            close_db()
            open_db(path)
            update_console(settings_window, f"Created/opened database: {path}")

    def open_existing_db():
        path = filedialog.askopenfilename(filetypes=[('SQLite DB', '*.sqlite'), ('All files', '*.*')])
        if path:
            close_db()
            open_db(path)
            update_console(settings_window, f"Opened database: {path}")
            load_events_from_db_to_state() # Automatically syncs DB to plot on open

    def show_db_summary():
        summary = db_summary()
        if summary:
            update_console(settings_window, f"DB summary: total={summary['total_events']}, labels={summary['by_label']}")

    def export_db_csv():
        export_db_to_csv()

    ctk.CTkButton(db_frame, text="New DB", command=create_new_db).pack(fill='x', padx=5, pady=2)
    ctk.CTkButton(db_frame, text="Open DB", command=open_existing_db).pack(fill='x', padx=5, pady=2)
    ctk.CTkButton(db_frame, text="Export DB CSV", command=export_db_csv).pack(fill='x', padx=5, pady=2)
    ctk.CTkButton(db_frame, text="DB Summary", command=show_db_summary).pack(fill='x', padx=5, pady=2)
    ctk.CTkButton(right_inner, text="Reset Y‑AutoScale", command=reset_zoom).pack(pady=5, fill='x')

    options_frame = ctk.CTkFrame(right_inner)
    options_frame.pack(pady=10, fill='x')
    ctk.CTkLabel(options_frame, text="Display & Detection Controls", font=("Arial", 14, "bold")).pack(pady=5)

    show_raw_var = tk.BooleanVar(value=state.plot_show_raw)
    show_events_var = tk.BooleanVar(value=state.plot_show_events)
    def update_plot_options():
        state.plot_show_raw = show_raw_var.get()
        state.plot_show_events = show_events_var.get()
        plot_data([ax_main], canvas, channel_checkboxes)
    ctk.CTkCheckBox(options_frame, text="Show raw channels", variable=show_raw_var, command=update_plot_options).pack(anchor='w', padx=10, pady=2)
    ctk.CTkCheckBox(options_frame, text="Show events", variable=show_events_var, command=update_plot_options).pack(anchor='w', padx=10, pady=2)

    ctk.CTkLabel(options_frame, text="Quick detection tuning:").pack(anchor='w', padx=10, pady=(10,2))
    min_amp_slider = ctk.CTkSlider(options_frame, from_=0, to=2000, number_of_steps=200)
    min_amp_slider.set(state.detection_min_event_amplitude)
    min_amp_slider.pack(fill='x', padx=10, pady=2)
    ctk.CTkLabel(options_frame, text="Minimum event amplitude", font=(None, 10)).pack(anchor='w', padx=10)

    slope_slider = ctk.CTkSlider(options_frame, from_=0.0, to=10.0, number_of_steps=100)
    slope_slider.set(state.detection_slope_threshold)
    slope_slider.pack(fill='x', padx=10, pady=2)
    ctk.CTkLabel(options_frame, text="Slope threshold", font=(None, 10)).pack(anchor='w', padx=10)

    prominence_slider = ctk.CTkSlider(options_frame, from_=0.1, to=10.0, number_of_steps=100)
    prominence_slider.set(state.detection_min_prominence)
    prominence_slider.pack(fill='x', padx=10, pady=2)
    ctk.CTkLabel(options_frame, text="Min prominence", font=(None, 10)).pack(anchor='w', padx=10)

    def apply_quick_tune():
        state.detection_min_event_amplitude = float(min_amp_slider.get())
        state.detection_slope_threshold = float(slope_slider.get())
        state.detection_min_prominence = float(prominence_slider.get())
        update_console(settings_window, f"Quick tune applied: min_amp={state.detection_min_event_amplitude:.1f}, slope={state.detection_slope_threshold:.2f}, prom={state.detection_min_prominence:.2f}")
        plot_data([ax_main], canvas, channel_checkboxes)
    ctk.CTkButton(options_frame, text="Apply quick tune", command=apply_quick_tune).pack(pady=8, padx=10, fill='x')

    window_frame = ctk.CTkFrame(right_inner)
    window_frame.pack(pady=5, fill='x')
    ctk.CTkLabel(window_frame, text="Display Window (samples):").pack(side='left', padx=5)
    window_spinbox = ctk.CTkEntry(window_frame, width=80)
    window_spinbox.insert(0, str(state.display_window_size))
    window_spinbox.pack(side='left', padx=5)
    def set_window_size():
        try:
            val = int(window_spinbox.get())
            if val > 0:
                state.display_window_size = val
                plot_data([ax_main], canvas, channel_checkboxes)
        except:
            pass
    ctk.CTkButton(window_frame, text="Set", command=set_window_size, width=50).pack(side='left', padx=5)
    def reset_to_full_view():
        state.display_window_size = 9999999
        window_spinbox.delete(0, tk.END)
        window_spinbox.insert(0, "Full")
        plot_data([ax_main], canvas, channel_checkboxes)
    ctk.CTkButton(window_frame, text="Full View", command=reset_to_full_view, width=70).pack(side='left', padx=5)

    # ===========================================================
    # WS COMMANDS PANEL
    # ===========================================================
    cmd_frame = ctk.CTkFrame(right_inner)
    cmd_frame.pack(pady=10, fill='x')
    ctk.CTkLabel(cmd_frame, text="📡 WebSocket Commands", font=("Arial", 14, "bold")).pack(pady=(4, 2))

    cmd_console_frame = ctk.CTkFrame(cmd_frame)
    cmd_console_frame.pack(fill='x', padx=4, pady=(0, 4))
    ctk.CTkLabel(cmd_console_frame, text="Last reply:", font=(None, 10)).pack(anchor='w', padx=4)
    cmd_reply_text = tk.Text(cmd_console_frame, wrap=tk.WORD, height=4,
                             bg='#13212f', fg='#8ff4a2', insertbackground='#8ff4a2')
    cmd_reply_text.pack(fill='x', padx=4, pady=2)

    def _set_cmd_reply(msg):
        cmd_reply_text.delete('1.0', tk.END)
        cmd_reply_text.insert('1.0', msg if msg else '')

    def _queue_and_show(cmd):
        send_command(cmd)
        _set_cmd_reply(f"[Sent] {cmd}")

    def _send_raw(event=None):
        raw = raw_cmd_entry.get().strip()
        if raw:
            _queue_and_show(raw)
            raw_cmd_entry.delete(0, tk.END)

    raw_cmd_entry = ctk.CTkEntry(cmd_frame, placeholder_text="Raw command (e.g. SET_LC|0|0|13|100)")
    raw_cmd_entry.pack(fill='x', padx=4, pady=2)
    raw_cmd_entry.bind('<Return>', _send_raw)
    ctk.CTkButton(cmd_frame, text="Send Raw Command", command=_send_raw).pack(fill='x', padx=4, pady=(0, 6))

    # Handler that captures any incoming WS message and shows last non-AUTO reply.
    # SENSOR_DATA is a high-rate stream; we still parse it into the plot but
    # we do NOT display it in the "Last reply" panel so important messages stay visible.
    def _on_ws_message_capture(msg):
        try:
            if msg is None:
                return
            text = msg if isinstance(msg, str) else str(msg)
            if text.startswith('SENSOR_DATA|'):
                return
            if len(text) > 400:
                text = text[:400] + "…"
            cmd_reply_text.delete('1.0', tk.END)
            cmd_reply_text.insert('1.0', text)
        except Exception:
            pass
    state.last_ws_capture = _on_ws_message_capture

    def _add_cmd_button(parent, label, command, row=None, col=0, colspan=1):
        b = ctk.CTkButton(parent, text=label,
                          command=lambda c=command: _queue_and_show(c))
        b.grid(row=row, column=col, columnspan=colspan, padx=2, pady=2, sticky='nsew')
        return b

    # --- System ---
    sys_box = ctk.CTkFrame(cmd_frame)
    sys_box.pack(fill='x', padx=4, pady=4)
    ctk.CTkLabel(sys_box, text="System", font=(None, 12, "bold")).grid(row=0, column=0, columnspan=3, sticky='w', padx=4)
    _add_cmd_button(sys_box, "CALIBRATE",        "CALIBRATE",        row=1, col=0)
    _add_cmd_button(sys_box, "GET_STATUS",       "GET_STATUS",       row=1, col=1)
    _add_cmd_button(sys_box, "GET_CPU",          "GET_CPU",          row=1, col=2)
    _add_cmd_button(sys_box, "GET_CONFIG",       "GET_CONFIG",       row=2, col=0)
    _add_cmd_button(sys_box, "GET_NOISE",        "GET_NOISE",        row=2, col=1)
    _add_cmd_button(sys_box, "GET_CALIB_STATUS", "GET_CALIB_STATUS", row=2, col=2)
    _add_cmd_button(sys_box, "GET_LOOP_GEOMETRY","GET_LOOP_GEOMETRY",row=3, col=0)
    _add_cmd_button(sys_box, "GET_SPEED_CONFIG", "GET_SPEED_CONFIG", row=3, col=1)
    _add_cmd_button(sys_box, "GET_SPEED_STATE",  "GET_SPEED_STATE",  row=3, col=2)
    _add_cmd_button(sys_box, "RESET",            "RESET",            row=2, col=0)  # alt row below
    # Use a separate row for RESET (danger) so it stands out
    danger_box = ctk.CTkFrame(cmd_frame)
    danger_box.pack(fill='x', padx=4, pady=(0,4))
    ctk.CTkLabel(danger_box, text="Danger Zone", font=(None, 12, "bold"), text_color="#dc3545").pack(anchor='w', padx=4)
    ctk.CTkButton(danger_box, text="🔄 RESET ESP32",
                  fg_color="#dc3545", hover_color="#b02a37",
                  command=lambda: (_queue_and_show("RESET"))).pack(fill='x', padx=4, pady=2)

    # --- Detector ---
    det_box = ctk.CTkFrame(cmd_frame)
    det_box.pack(fill='x', padx=4, pady=4)
    ctk.CTkLabel(det_box, text="Detector / Classifier", font=(None, 12, "bold")).grid(row=0, column=0, columnspan=3, sticky='w', padx=4)
    _add_cmd_button(det_box, "SET_LC|0|0|13|100", "SET_LC|0|0|13|100", row=1, col=0)
    _add_cmd_button(det_box, "SET_LC|0|1|13|100", "SET_LC|0|1|13|100", row=1, col=1)
    _add_cmd_button(det_box, "SET_LC|1|0|13|100", "SET_LC|1|0|13|100", row=1, col=2)
    _add_cmd_button(det_box, "SET_THRESHOLD|enter|0.0008", "SET_THRESHOLD|enter|0.0008", row=2, col=0)
    _add_cmd_button(det_box, "SET_DETECTOR|3|6|1.8|0.70|0.55", "SET_DETECTOR|3|6|1.8|0.70|0.55", row=2, col=1)
    _add_cmd_button(det_box, "SET_AUTO_THRESH|1|6.0|3.0", "SET_AUTO_THRESH|1|6.0|3.0", row=2, col=2)
    _add_cmd_button(det_box, "SET_EVENT_RANGE|20|5000", "SET_EVENT_RANGE|20|5000", row=3, col=0)
    _add_cmd_button(det_box, "SET_DEFAULT_KMH|90", "SET_DEFAULT_KMH|90", row=3, col=1)
    _add_cmd_button(det_box, "SET_CLASSIFY|...", "SET_CLASSIFY|2.4|4.0|5.0|6.5|11.0|8.0|10.5|14.0|14.0|0.35|80|45|160|300|0.00001|0.00008|0.00020|1.6|1.2|0.35|0.75|0.33|0.50|0.20|0.40|0.35|0.18", row=3, col=2)

    # --- Speed / Loops ---
    sp_box = ctk.CTkFrame(cmd_frame)
    sp_box.pack(fill='x', padx=4, pady=4)
    ctk.CTkLabel(sp_box, text="Speed / Loops", font=(None, 12, "bold")).grid(row=0, column=0, columnspan=3, sticky='w', padx=4)
    _add_cmd_button(sp_box, "CONFIG|1|0.4|0|0|1|0", "CONFIG|1|0.4|0|0|1|0", row=1, col=0)
    _add_cmd_button(sp_box, "CONFIG_SET|1|1|0.4|0|1|1|1", "CONFIG_SET|1|1|0.4|0|1|1|1", row=1, col=1)
    _add_cmd_button(sp_box, "SET_ADJACENT|0|0|1|0|1", "SET_ADJACENT|0|0|1|0|1", row=1, col=2)

    # --- Rules / Report ---
    rep_box = ctk.CTkFrame(cmd_frame)
    rep_box.pack(fill='x', padx=4, pady=4)
    ctk.CTkLabel(rep_box, text="Rules / Report", font=(None, 12, "bold")).grid(row=0, column=0, columnspan=3, sticky='w', padx=4)
    _add_cmd_button(rep_box, "SET_RULES|100|5|30|1.2|20000|100|0.25|0", "SET_RULES|100|5|30|1.2|20000|100|0.25|0", row=1, col=0)
    _add_cmd_button(rep_box, "SET_REPORT_INTERVAL|20", "SET_REPORT_INTERVAL|20", row=1, col=1)
    _add_cmd_button(rep_box, "SET_REPORT_ENABLE|1", "SET_REPORT_ENABLE|1", row=1, col=2)
    _add_cmd_button(rep_box, "SET_REPORT_CLEAR|0", "SET_REPORT_CLEAR|0", row=2, col=0)
    _add_cmd_button(rep_box, "GET_REPORT", "GET_REPORT", row=2, col=1)
    _add_cmd_button(rep_box, "RESET_STATS", "RESET_STATS", row=2, col=2)

    # --- MQTT / Technician ---
    mq_box = ctk.CTkFrame(cmd_frame)
    mq_box.pack(fill='x', padx=4, pady=4)
    ctk.CTkLabel(mq_box, text="MQTT / Technician", font=(None, 12, "bold")).grid(row=0, column=0, columnspan=3, sticky='w', padx=4)
    _add_cmd_button(mq_box, "GET_MQTT_CFG", "GET_MQTT_CFG", row=1, col=0)
    _add_cmd_button(mq_box, "GET_WIFI",     "GET_WIFI",     row=1, col=1)
    _add_cmd_button(mq_box, "GET_SENSOR_LC","GET_SENSOR_LC",row=1, col=2)
    _add_cmd_button(mq_box, "GET_LOOP_CFG", "GET_LOOP_CFG", row=2, col=0)
    _add_cmd_button(mq_box, "GET_TRAFFIC_RULES","GET_TRAFFIC_RULES", row=2, col=1)
    _add_cmd_button(mq_box, "GET_REPORT_CFG","GET_REPORT_CFG", row=2, col=2)
    _add_cmd_button(mq_box, "GET_DEFAULT_KMH","GET_DEFAULT_KMH", row=3, col=0)
    _add_cmd_button(mq_box, "SAVE_ALL",     "SAVE_ALL",     row=3, col=1)
    _add_cmd_button(mq_box, "GET_LOOP_GEOMETRY","GET_LOOP_GEOMETRY", row=3, col=2)

    # Make grid columns expand evenly
    for f in (sys_box, det_box, sp_box, rep_box, mq_box):
        for c in range(3):
            f.grid_columnconfigure(c, weight=1, uniform='cmd')
    # ===========================================================

    def periodic():
        if not hasattr(periodic, 'init_done'):
            for i in range(8):
                if len(state.raw_data[i]) >= 10:
                    first10 = state.raw_data[i][:10]
                    if all(v == 0 for v in first10):
                        state.channel_active[i] = False
                        try:
                            channel_checkboxes[i].deselect()
                        except Exception:
                            pass
            periodic.init_done = True

        if state.running and not state.paused:
            if not hasattr(periodic, 'plot_frame_count'):
                periodic.plot_frame_count = 0
            periodic.plot_frame_count += 1
            if periodic.plot_frame_count >= 3:  
                plot_data([ax_main], canvas, channel_checkboxes)
                periodic.plot_frame_count = 0
        root.after(200, periodic)

    root.after(500, periodic)
    root.after(1500, periodic_stats_refresh)
    return root

# ============================================================
# MAIN ENTRY POINT
# ============================================================

if __name__ == "__main__":
    initialize_db()
    command_thread = threading.Thread(target=process_command_queue, daemon=True)
    command_thread.start()
    heartbeat_thread = threading.Thread(target=heartbeat_worker, daemon=True)
    heartbeat_thread.start()
    ws_thread = threading.Thread(target=run_websocket, daemon=True)
    ws_thread.start()
    state.running = True
    root = create_main_window()
    root.mainloop()
