/*
 * Hip-Hop / High Performance Hybrid Audio Plugins
 * Copyright (C) 2021 Luciano Iam <oss@lucianoiam.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

import 'wasi'
import DISTRHO from './distrho-plugin'

const PUNCH: f32 = 4.0
const DECAY: f32 = 1.0

export default class JitDrumExamplePlugin extends DISTRHO.Plugin implements DISTRHO.PluginInterface {

    private sr: f32 // samplerate
    private t: i32  // frames since note start
    private f: f32  // note frequency
    private a: f32  // amplitude

    getLabel(): string {
        return 'JITDrum'
    }

    getMaker(): string {
        return 'Luciano Iam, George Stagas'
    }

    getLicense(): string {
        return 'ISC'
    }

    getVersion(): u32 {
        return DISTRHO.d_version(1, 0, 0)
    }

    getUniqueId(): i64 {
        return DISTRHO.d_sconst('HHjd')
    }

    initParameter(index: u32, parameter: DISTRHO.Parameter): void {
        // empty implementation
    }

    getParameterValue(index: u32): f32 {
        return 0 // empty implementation
    }

    setParameterValue(index: u32, value: f32): void {
        // empty implementation
    }

    initProgramName(index: u32, programName: DISTRHO.StringWrapper): void {
        // empty implementation
    }

    loadProgram(index: u32): void {
        // empty implementation
    }

    initState(index: u32, stateKey: DISTRHO.StringWrapper, defaultStateValue: DISTRHO.StringWrapper): void {
        // empty implementation
    }

    setState(key: string, value: string): void {
        // empty implementation
    }
    
    getState(key: string): string {
        return '' // empty implementation
    }

    activate(): void {
        this.sr = this.getSampleRate()
    }

    deactivate(): void {
        // empty implementation
    }

    run(inputs: Float32Array[], outputs: Float32Array[], midiEvents: DISTRHO.MidiEvent[]): void {
        // TODO: avoid clicks
        
        if ((midiEvents.length > 0) && (midiEvents[0].data[0] & 0xf0) == 0x90) {
            this.t = 0
            this.f = 440 * Mathf.pow(2, (<f32>midiEvents[0].data[1] - 69) / 12)
            this.a = <f32>midiEvents[0].data[2] / 0xff
        }

        let t: f32
        let k: f32

        for (let i = 0; i < outputs[0].length; ++i) {
            t = <f32>this.t / this.sr
            k = this.a * Mathf.sin(this.f * Mathf.exp(PUNCH * -t)) * Mathf.exp(DECAY * -t)
            outputs[0][i] = outputs[1][i] = k
            this.t += 1
        }
    }

}
