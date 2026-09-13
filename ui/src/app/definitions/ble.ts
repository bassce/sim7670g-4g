export interface BLEDevice {
    name: string; mac: string; addressType: number; rssi: number; advertisedOBD: boolean;
}
export interface BLEStatus {
    supported: boolean; state: string; connected: boolean; busy: boolean; jobId: number;
    name: string; mac: string; protocol: string; error: string; devices: BLEDevice[];
}
export const BLE_LABELS: Record<string, string> = {
    disabled: 'OBD connection disabled', queued: 'Waiting for Bluetooth',
    scanning: 'Scanning nearby devices', scan_complete: 'Scan complete',
    connecting: 'Connecting Bluetooth', initializing: 'Bluetooth connected — initializing ELM327',
    connected: 'Online — ELM327 initialized', disconnected: 'Disconnected',
    bluetooth_error: 'Bluetooth connection failed', elm_error: 'ELM327 initialization failed',
    not_found: 'Configured device not found', save_error: 'Could not save connection'
};
