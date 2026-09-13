import { Component, DestroyRef, inject, OnInit } from '@angular/core';
import { CommonModule } from '@angular/common';
import { Router } from '@angular/router';
import { takeUntilDestroyed } from '@angular/core/rxjs-interop';
import { catchError, exhaustMap, of, timer } from 'rxjs';
import { ApiService } from '../services/api.service';
import { BLEDevice, BLEStatus, BLE_LABELS } from '../definitions/ble';
import { OBD2Protocol } from '../definitions';

@Component({
    selector: 'ui-bluetooth', standalone: true, imports: [CommonModule],
    templateUrl: './bluetooth.component.html'
})
export class BluetoothComponent implements OnInit {
    status?: BLEStatus;
    protocol?: string;
    error = '';
    requesting = false;
    labels = BLE_LABELS;
    private connectJob?: number;
    private selected?: BLEDevice;
    private api = inject(ApiService);
    private router = inject(Router);
    private destroyRef = inject(DestroyRef);
    ngOnInit() {
        this.api.settings().pipe(takeUntilDestroyed(this.destroyRef)).subscribe({
            next: settings => this.protocol = this.api.settingsDraft?.obd2?.protocol ?? settings.obd2?.protocol ?? '0',
            error: () => this.error = 'Cannot load Protocol. Return to settings and try again.'
        });
        timer(0, 1000).pipe(exhaustMap(() => this.api.obdStatus().pipe(catchError(() => of(null)))),
            takeUntilDestroyed(this.destroyRef)).subscribe(status => {
                if (!status) { this.error = 'Cannot reach the device. Check the configuration WiFi connection.'; return; }
                this.status = status;
                if (this.connectJob === status.jobId && !status.busy) {
                    if (status.connected && this.selected) {
                        if (this.api.settingsDraft) {
                            this.api.settingsDraft.obd2 = {...this.api.settingsDraft.obd2,
                                name: this.selected.name, mac: this.selected.mac,
                                addressType: this.selected.addressType, disable: false, protocol: this.protocol as OBD2Protocol};
                        }
                        this.back();
                    } else this.error = status.error || this.labels[status.state] || 'Connection failed. Please retry.';
                    this.connectJob = undefined;
                }
            });
    }
    scan() {
        this.error = ''; this.requesting = true; this.connectJob = undefined;
        this.api.scanBluetooth().pipe(takeUntilDestroyed(this.destroyRef)).subscribe({
            next: status => { this.status = status; this.requesting = false; },
            error: () => { this.requesting = false; this.error = 'Cannot start scan. Bluetooth may be busy; please retry.'; }
        });
    }
    connect(device: BLEDevice) {
        if (!this.protocol) return;
        this.error = ''; this.requesting = true; this.selected = device;
        this.api.connectBluetooth(device, this.protocol).pipe(takeUntilDestroyed(this.destroyRef)).subscribe({
            next: status => { this.status = status; this.connectJob = status.jobId; this.requesting = false; },
            error: () => { this.requesting = false; this.error = 'Cannot start connection. Scan again if the list has changed.'; }
        });
    }
    back() {
        const destination = this.api.bluetoothReturnUrl;
        this.router.navigateByUrl(destination === '/' || destination === '/settings' ? destination : '/settings');
    }
}
