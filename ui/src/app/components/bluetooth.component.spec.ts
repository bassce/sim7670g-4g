import { TestBed, fakeAsync, tick } from '@angular/core/testing';
import { Router } from '@angular/router';
import { of, throwError } from 'rxjs';
import { BluetoothComponent } from './bluetooth.component';
import { ApiService } from '../services/api.service';
import { BLEDevice, BLEStatus } from '../definitions/ble';

describe('Bluetooth connection page', () => {
    const device: BLEDevice = {name: 'IOS-Vlink', mac: '11:22:33:44:55:66', addressType: 1,
        rssi: -52, advertisedOBD: true};
    let status: BLEStatus;
    let api: any;
    let router: any;
    beforeEach(() => {
        status = {supported: true, state: 'scan_complete', connected: false, busy: false, jobId: 1,
            name: '', mac: '', protocol: '7', error: '', devices: [device]};
        api = {
            bluetoothReturnUrl: '/settings',
            settingsDraft: {wifi: {ssid: 'Unsaved WiFi'}, obd2: {protocol: '7', disable: true}},
            settings: () => of({obd2: {protocol: '0'}}),
            obdStatus: () => of({...status}),
            scanBluetooth: jasmine.createSpy().and.callFake(() => {
                status = {...status, busy: true, state: 'queued', jobId: 2, devices: []}; return of({...status});
            }),
            connectBluetooth: jasmine.createSpy().and.callFake(() => {
                status = {...status, busy: true, state: 'queued', jobId: 3}; return of({...status});
            })
        };
        router = {navigateByUrl: jasmine.createSpy()};
        TestBed.configureTestingModule({imports: [BluetoothComponent], providers: [
            {provide: ApiService, useValue: api}, {provide: Router, useValue: router}
        ]});
    });
    it('scans on demand and disables actions while busy', fakeAsync(() => {
        const f = TestBed.createComponent(BluetoothComponent); f.detectChanges(); tick(); f.detectChanges();
        expect(api.scanBluetooth).not.toHaveBeenCalled();
        f.componentInstance.scan(); f.detectChanges();
        expect(api.scanBluetooth).toHaveBeenCalledTimes(1);
        expect(f.nativeElement.querySelector('.btn-primary').disabled).toBeTrue();
        f.destroy();
    }));
    it('keeps Protocol and random address type, and returns only after successful initialization and saving', fakeAsync(() => {
        const f = TestBed.createComponent(BluetoothComponent); f.detectChanges(); tick();
        f.componentInstance.connect(device);
        expect(api.connectBluetooth).toHaveBeenCalledWith(device, '7');
        status = {...status, state: 'initializing'}; tick(1000);
        expect(router.navigateByUrl).not.toHaveBeenCalled();
        status = {...status, state: 'connected', connected: true, busy: true}; tick(1000);
        expect(router.navigateByUrl).not.toHaveBeenCalled();
        status = {...status, busy: false}; tick(1000);
        expect(router.navigateByUrl).toHaveBeenCalledOnceWith('/settings');
        expect(api.settingsDraft.wifi.ssid).toBe('Unsaved WiFi');
        expect(api.settingsDraft.obd2.mac).toBe(device.mac);
        expect(api.settingsDraft.obd2.addressType).toBe(1);
        expect(api.settingsDraft.obd2.disable).toBeFalse();
        f.destroy();
    }));
    it('does not treat an old connected state as this connection succeeding', fakeAsync(() => {
        const f = TestBed.createComponent(BluetoothComponent); f.detectChanges(); tick();
        f.componentInstance.connect(device);
        status = {...status, jobId: 2, connected: true, busy: false, state: 'connected'}; tick(1000);
        expect(router.navigateByUrl).not.toHaveBeenCalled(); f.destroy();
    }));
    it('shows initialization failures and preserves the prior configuration', fakeAsync(() => {
        const f = TestBed.createComponent(BluetoothComponent); f.detectChanges(); tick();
        f.componentInstance.connect(device);
        status = {...status, state: 'elm_error', busy: false, error: 'Check Protocol'}; tick(1000);
        expect(router.navigateByUrl).not.toHaveBeenCalled();
        expect(f.componentInstance.error).toBe('Check Protocol');
        expect(api.settingsDraft.obd2.mac).toBeUndefined(); f.destroy();
    }));
    it('recovers the buttons after a rejected scan request', fakeAsync(() => {
        api.scanBluetooth.and.returnValue(throwError(() => new Error('busy')));
        const f = TestBed.createComponent(BluetoothComponent); f.detectChanges(); tick();
        f.componentInstance.scan();
        expect(f.componentInstance.requesting).toBeFalse();
        expect(f.componentInstance.error).toContain('busy'); f.destroy();
    }));
    it('returns to the device page when opened there', fakeAsync(() => {
        api.bluetoothReturnUrl = '/';
        const f = TestBed.createComponent(BluetoothComponent); f.detectChanges(); tick();
        f.componentInstance.back();
        expect(router.navigateByUrl).toHaveBeenCalledWith('/'); f.destroy();
    }));
});
