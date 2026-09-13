import { TestBed } from '@angular/core/testing';
import { DomSanitizer } from '@angular/platform-browser';
import { ApiService } from '../services/api.service';
import { ToastService } from '../services/toast.service';
import { OBDState } from '../definitions';
import { OBDStatesComponent } from './obdStates.component';

describe('OBD query configuration', () => {
    let component: OBDStatesComponent;
    beforeEach(() => {
        TestBed.configureTestingModule({providers: [
            {provide: ApiService, useValue: {}},
            {provide: DomSanitizer, useValue: {}},
            {provide: ToastService, useValue: {}}
        ]});
        component = TestBed.runInInjectionContext(() => new OBDStatesComponent());
    });
    const sample = (): OBDState => ({
        type: 0, valueType: 'float', enabled: true, visible: true, interval: 1000,
        name: 'batteryCurrent', description: 'Battery current', unit: 'A',
        measurement: true, diagnostic: false,
        pid: {service: 1, pid: 154, header: 417018865, numResponses: 0,
            numExpectedBytes: 6, responseFormat: 0, scaleFactor: '0.1', bias: 0,
            protocol: 7, receiveHeader: 417001733, flowControlHeader: 0,
            flowControlData: 3145728, dataOffset: 4, dataLength: 2, signedValue: true},
        value: {format: '%3.1f'}
    } as OBDState);
    it('preserves all capture-derived settings through import/edit/export', () => {
        const original = sample();
        const form = component.buildState(original);
        expect(form.valid).toBeTrue();
        form.get('description').setValue('Updated description');
        const reimported = component.buildState(JSON.parse(JSON.stringify(form.value)));
        expect(reimported.get('pid').value).toEqual(original.pid);
    });
    it('keeps old PID configurations usable with automatic defaults', () => {
        const original = sample();
        for (const key of ['protocol', 'receiveHeader', 'flowControlHeader', 'flowControlData', 'dataOffset', 'dataLength', 'signedValue'])
            delete (original.pid as any)[key];
        const form = component.buildState(original);
        expect(form.valid).toBeTrue();
        expect(form.get('pid.protocol').value).toBe(0);
        expect(form.get('pid.receiveHeader').value).toBe(0);
        expect(form.get('pid.dataLength').value).toBe(0);
    });
    it('rejects truncated fields and incompatible CAN addresses', () => {
        const form = component.buildState(sample());
        form.get('pid.dataOffset').setValue(5);
        expect(form.get('pid').hasError('invalidQuery')).toBeTrue();
        form.get('pid.dataOffset').setValue(4);
        form.get('pid.protocol').setValue(6);
        expect(form.get('pid').hasError('invalidQuery')).toBeTrue();
        form.get('pid.protocol').setValue(7);
        form.get('pid.flowControlHeader').setValue(402391163);
        form.get('pid.flowControlData').setValue(0x300080);
        expect(form.get('pid').hasError('invalidQuery')).toBeTrue();
        form.get('pid.flowControlData').setValue(0x300000);
        expect(form.valid).toBeTrue();
    });
});
