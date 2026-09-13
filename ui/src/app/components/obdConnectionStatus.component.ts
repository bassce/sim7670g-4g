import { Component, DestroyRef, EventEmitter, inject, OnInit, Output } from '@angular/core';
import { CommonModule } from '@angular/common';
import { Router } from '@angular/router';
import { takeUntilDestroyed } from '@angular/core/rxjs-interop';
import { catchError, exhaustMap, of, timer } from 'rxjs';
import { ApiService } from '../services/api.service';
import { BLEStatus, BLE_LABELS } from '../definitions/ble';

@Component({
    selector: 'ui-obd-connection-status', standalone: true, imports: [CommonModule],
    template: `
    @if (status?.supported) {
      <div class="border rounded p-3 mb-3">
        <div class="d-flex flex-wrap align-items-center justify-content-between gap-2">
          <strong>Bluetooth OBD</strong>
          <button type="button" class="btn btn-outline-primary btn-sm" (click)="open()">Choose device</button>
        </div>
        <p class="mb-1 mt-2" role="status" [class.text-success]="status.connected && !unreachable">
          {{ unreachable ? 'Connection status unavailable' : labels[status.state] || status.state }}
        </p>
        @if (status.mac) {
          <div>{{ status.name || 'Unnamed device' }} <span class="text-body-secondary">{{ status.mac }}</span></div>
        }
        <small class="text-body-secondary">Adapter connection does not confirm that vehicle PIDs are supported.</small>
      </div>
    }`
})
export class OBDConnectionStatusComponent implements OnInit {
    @Output() beforeOpen = new EventEmitter<void>();
    status?: BLEStatus;
    unreachable = false;
    labels = BLE_LABELS;
    private api = inject(ApiService);
    private router = inject(Router);
    private destroyRef = inject(DestroyRef);
    ngOnInit() {
        timer(0, 2000).pipe(exhaustMap(() => this.api.obdStatus().pipe(catchError(() => of(null)))),
            takeUntilDestroyed(this.destroyRef)).subscribe(status => {
                this.unreachable = !status;
                if (status) this.status = status;
            });
    }
    open() {
        this.beforeOpen.emit();
        this.api.bluetoothReturnUrl = this.router.url;
        this.router.navigate(['/bluetooth']);
    }
}
