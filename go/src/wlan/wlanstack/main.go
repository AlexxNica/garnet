// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"app/context"
	"fidl/bindings"

	"syscall/zx"
	"syscall/zx/mxerror"

	"garnet/public/lib/wlan/fidl/wlan_service"
	"netstack/watcher"
	"wlan/wlan"

	"log"
	"sync"
)

type Wlanstack struct {
	cfg   *wlan.Config
	apCfg *wlan.APConfig
	stubs []*bindings.Stub

	mu     sync.Mutex
	client []*wlan.Client
}

func (ws *Wlanstack) Scan(sr wlan_service.ScanRequest) (res wlan_service.ScanResult, err error) {
	cli := ws.getCurrentClient()
	if cli == nil {
		return wlan_service.ScanResult{
			wlan_service.Error{
				wlan_service.ErrCode_NotFound,
				"No wlan interface found"},
			nil,
		}, nil
	}
	respC := make(chan *wlan.CommandResult, 1)
	cli.PostCommand(wlan.CmdScan, &sr, respC)

	resp := <-respC
	if resp.Err != nil {
		return wlan_service.ScanResult{
			*resp.Err,
			nil,
		}, nil
	}
	waps, ok := resp.Resp.([]wlan.AP)
	if !ok {
		return wlan_service.ScanResult{
			wlan_service.Error{
				wlan_service.ErrCode_Internal,
				"Internal error"},
			nil,
		}, nil
	}
	aps := []wlan_service.Ap{}
	for _, wap := range waps {
		bssid := make([]uint8, len(wap.BSSID))
		copy(bssid, wap.BSSID[:])
		// Currently we indicate the AP is secure if it supports RSN.
		// TODO: Check if AP supports other types of security mechanism (e.g. WEP)
		is_secure := wap.BSSDesc.Rsn != nil
		// TODO: Revisit this RSSI conversion.
		last_rssi := int8(wap.LastRSSI)
		ap := wlan_service.Ap{bssid, wap.SSID, int32(last_rssi), is_secure}
		aps = append(aps, ap)
	}
	return wlan_service.ScanResult{
		wlan_service.Error{wlan_service.ErrCode_Ok, "OK"},
		&aps,
	}, nil
}

func (ws *Wlanstack) Connect(sc wlan_service.ConnectConfig) (wserr wlan_service.Error, err error) {
	cli := ws.getCurrentClient()
	if cli == nil {
		return wlan_service.Error{
			wlan_service.ErrCode_NotFound,
			"No wlan interface found"}, nil
	}
	cfg := wlan.NewConfig()
	cfg.SSID = sc.Ssid
	cfg.Password = sc.PassPhrase
	cfg.ScanInterval = int(sc.ScanInterval)
	respC := make(chan *wlan.CommandResult, 1)
	cli.PostCommand(wlan.CmdSetScanConfig, cfg, respC)

	resp := <-respC
	if resp.Err != nil {
		return *resp.Err, nil
	}
	return wlan_service.Error{wlan_service.ErrCode_Ok, "OK"}, nil
}

func (ws *Wlanstack) Disconnect() (wserr wlan_service.Error, err error) {
	cli := ws.getCurrentClient()
	if cli == nil {
		return wlan_service.Error{wlan_service.ErrCode_NotFound, "No wlan interface found"}, nil
	}
	respC := make(chan *wlan.CommandResult, 1)
	var noArgs interface{}
	cli.PostCommand(wlan.CmdDisconnect, noArgs, respC)

	resp := <-respC
	if resp.Err != nil {
		return *resp.Err, nil
	}
	return wlan_service.Error{wlan_service.ErrCode_Ok, "OK"}, nil
}

func (ws *Wlanstack) Bind(r wlan_service.Wlan_Request) {
	s := r.NewStub(ws, bindings.GetAsyncWaiter())
	ws.stubs = append(ws.stubs, s)
	go func() {
		for {
			if err := s.ServeRequest(); err != nil {
				if mxerror.Status(err) != zx.ErrPeerClosed {
					log.Println(err)
				}
				break
			}
		}
	}()
}

func main() {
	log.SetFlags(0)
	log.SetPrefix("wlanstack: ")
	log.Print("started")

	ws := &Wlanstack{}

	ctx := context.CreateFromStartupInfo()
	ctx.OutgoingService.AddService(&wlan_service.Wlan_ServiceBinder{ws})
	ctx.Serve()

	ws.readConfigFile()
	ws.readAPConfigFile()

	const ethdir = "/dev/class/ethernet"
	wt, err := watcher.NewWatcher(ethdir)
	if err != nil {
		log.Fatalf("ethernet: %v", err)
	}
	log.Printf("watching for wlan devices")

	for name := range wt.C {
		path := ethdir + "/" + name
		if err := ws.addDevice(path); err != nil {
			log.Printf("failed to add wlan device %s: %v", path, err)
		}
	}
}

func (ws *Wlanstack) readConfigFile() {
	// TODO(tkilbourn): monitor this file for changes
	// TODO(tkilbourn): replace this with a FIDL interface
	const configFile = "/pkg/data/config.json"
	// Once we remove the overrideFile, remove "system" access from meta/sandbox.
	const overrideFile = "/system/data/wlanstack/override.json"

	cfg, err := wlan.ReadConfigFromFile(overrideFile)
	if err != nil {
		if cfg, err = wlan.ReadConfigFromFile(configFile); err != nil {
			log.Printf("[W] could not open config (%v)", configFile)
			return
		}
	}
	ws.cfg = cfg
}

func (ws *Wlanstack) readAPConfigFile() {
	// TODO(hahnr): Replace once FIDL api is in place
	const apConfigFile = "/pkg/data/ap_config.json"

	cfg, err := wlan.ReadAPConfigFromFile(apConfigFile)
	if err != nil {
		log.Printf("[W] could not open config (%v)", apConfigFile)
		return
	}
	ws.apCfg = cfg
}

func (ws *Wlanstack) addDevice(path string) error {
	log.Printf("trying ethernet device %q", path)

	cli, err := wlan.NewClient(path, ws.cfg, ws.apCfg)
	if err != nil {
		return err
	}
	if cli == nil {
		// the device is not wlan. skip
		return nil
	}
	log.Printf("found wlan device %q", path)

	ws.mu.Lock()
	ws.client = append(ws.client, cli)
	ws.mu.Unlock()

	go cli.Run()
	return nil
}

func (ws *Wlanstack) getCurrentClient() *wlan.Client {
	// Use the last client.
	// TODO: Add a mechanism to choose a client
	ws.mu.Lock()
	defer ws.mu.Unlock()
	ncli := len(ws.client)
	if ncli > 0 {
		return ws.client[ncli-1]
	} else {
		return nil
	}
}
