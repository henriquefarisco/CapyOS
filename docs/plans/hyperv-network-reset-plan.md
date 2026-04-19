# Hyper-V Network Reset Plan

## Objective

Close the Hyper-V Gen2 networking gap without regressing the already stable PCI
paths used by QEMU and VMware.

This plan intentionally resets the workstream around the current field
symptoms, instead of continuing from older assumptions.

## Current field reality

- VMware plus e1000: working with DHCP and `hey`.
- QEMU plus e1000: working with IPv4, DHCP, DNS and ICMP.
- Hyper-V Gen2 plus NetVSC/StorVSC: not working.

Latest observed Hyper-V state:

- `runtime-native prepare-input` no longer participates in the recommended
  hybrid flow because even the hypercall-only path can correlate with reboot on
  real Hyper-V.
- `runtime-native prepare-storage` does not produce a usable storage backend.
- `net-status` still reports:
  - `driver=hyperv-netvsc`
  - `runtime=driver-missing`
  - `platform=hybrid`
  - `bootsvc=active`
  - `ebs=wait-storage-firmware` or `ebs=wait-input`
  - `storvsc ... phase=disabled gate=wait-platform`
- The VM still reboots intermittently after manual runtime preparation.

## Fresh diagnosis

The blocking problem is not DHCP, DNS or the CLI.

The blocking problem is the platform transition sequence:

1. input still depends on firmware when Boot Services are active
2. storage still depends on firmware when Boot Services are active
3. `SynIC/VMBus` needs a kernel-owned interrupt/runtime bridge before synthetic
   devices can be promoted safely
4. NetVSC depends on VMBus and synthetic storage reaching a safe native state
5. background promotion paths are still too eager for Hyper-V

## Working rule from now on

While the platform is hybrid:

- manual commands may only prepare passive state
- no automatic VMBus connect
- no offer refresh
- no open channel
- no StorVSC control handshake
- no NetVSC control handshake

Only after the platform reaches a safe pre-EBS bridge and then leaves hybrid
may synthetic storage and network advance to active channel or control phases.

## Delivery phases

### H1. Stabilize hybrid runtime

Goals:

- stop all background StorVSC promotion while `Boot Services` are active
- stop all background NetVSC promotion while `Boot Services` are active
- remove `prepare-input` from the exposed hybrid recovery sequence
- keep `prepare-storage` limited to passive bus preparation only

Acceptance:

- no spontaneous reboot after login
- no spontaneous reboot after `runtime-native show` / idle time in hybrid mode
- no spontaneous reboot after `prepare-storage`
- runtime counters stop growing uncontrollably while hybrid

### H2. Make native-runtime gate truthful

Goals:

- `ExitBootServices` must remain blocked while active storage uses firmware
- synthetic storage must not count as native until real backend switchover
- diagnostics must show one consistent truth in boot, CLI and runtime dump

Acceptance:

- `ebs=wait-storage-firmware` only clears after real storage switchover
- `storage-fw`, `storage-native` and `storage-synth` stop contradicting each other

### H3. Split VMBus transport into explicit phases

Required phases:

1. hypercall prepared
2. SynIC prepared
3. VMBus contact connected
4. offers cached
5. channel opened
6. control handshake done

Goals:

- represent these phases explicitly in runtime state
- never jump from phase 1 to active channel or control in one command

Acceptance:

- every phase change is observable in diagnostics
- every phase can be validated independently in Hyper-V

### H3b. Add a pre-EBS native bridge

Goals:

- preserve the x64 bridge code only as an internal experiment hook
- remove `prepare-bridge` from the recommended hybrid manual sequence
- keep the safe hybrid path limited to `prepare-input` as hypercall-only
- keep any `SynIC` or `VMBus connect` work outside the exposed hybrid flow

Acceptance:

- `runtime-native show` reports both table state and VMBus transport stage
- `prepare-bridge` stays disabled in hybrid mode
- no automatic `SynIC/contact/offers` while still hybrid

### H3c. Split `SynIC` from `connect`

Goals:

- keep `prepare-synic` out of the recommended hybrid manual flow until the
  platform transition is stable
- keep `INITIATE_CONTACT`, `REQUESTOFFERS` and channel open disabled
  while validating `SynIC-only`
- make diagnostics show `vmbus=off|hypercall|synic|connected`

Acceptance:

- `runtime-native show` prefers `next=prepare-input` over any bridge step
- `prepare-synic` remains disabled in hybrid mode
- reboots can no longer be attributed to a recommended pre-EBS bridge step

### H4. Promote StorVSC first

Goals:

- bring StorVSC to a usable state before touching NetVSC
- only use the pre-EBS bridge to reach `SynIC/contact/offers`, never the old
  hybrid path
- switch active storage backend away from firmware
- prove read path over StorVSC before attempting EBS

Acceptance:

- active storage backend is no longer `efi-blockio`
- `storage-fw=off`
- storage reads remain stable after reboot

### H5. Exit hybrid safely

Goals:

- execute `ExitBootServices` only after:
  - input safe enough
  - storage backend no longer on firmware
- preserve shell usability after the transition

Acceptance:

- `bootsvc=inactive`
- `platform=native`
- no reboot loop

### H6. Promote NetVSC

Goals:

- cache NetVSC offer
- open channel
- complete NetVSP plus RNDIS control handshake
- expose a real L2 runtime to the common stack

Acceptance:

- `runtime=ready`
- `ready=yes`
- stable channel and control phases

### H7. Validate end-to-end networking

Goals:

- DHCP lease
- DNS resolution
- `hey` to gateway and hostname
- reboot persistence

Acceptance:

- `net-mode dhcp` succeeds
- `hey <host>` succeeds
- networking survives reboot

## Code structure target

### Common stack

Keep shared network logic platform-agnostic:

- driver abstraction
- ARP
- IPv4
- ICMP
- UDP
- DHCP
- DNS
- request layer later

This path is already mostly correct because VMware and QEMU work.

### Hyper-V-specific layers

Keep Hyper-V isolated in these layers:

- `vmbus_transport`
  - hypercall
  - SynIC
  - post/wait message
- `vmbus_core`
  - contact
  - offers
  - connection state
- `storvsc_*`
  - storage-specific state machine
  - storage switchover logic
- `netvsc_*`
  - network-specific state machine
  - NetVSP and RNDIS handshake
- `*_gate` and `*_coordinator`
  - platform gating
  - safe sequencing

### Validation strategy

#### QEMU and VMware

Use them to validate:

- common stack correctness
- CLI correctness
- DHCP, DNS and ICMP logic
- regression testing

#### Hyper-V

Use it only for:

- platform transition
- VMBus behavior
- StorVSC behavior
- NetVSC behavior

Do not use Hyper-V to debug generic DHCP or DNS logic unless the platform path
is already native and stable.

## Immediate next actions

1. stop background StorVSC promotion in hybrid runtime
2. audit why `prepare-input` can still correlate with later reboot
3. make the native-runtime gate and storage state fully consistent
4. keep the pre-EBS native bridge out of the exposed hybrid sequence
5. then retest Hyper-V stability before any new functional activation
