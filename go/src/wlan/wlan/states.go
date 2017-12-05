// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package wlan

import (
	mlme "garnet/public/lib/wlan/fidl/wlan_mlme"
	mlme_ext "garnet/public/lib/wlan/fidl/wlan_mlme_ext"
	"garnet/public/lib/wlan/fidl/wlan_service"
	"wlan/eapol"
	"wlan/eapol/handshake"
	"wlan/wlan/elements"

	"fmt"
	"log"
	"time"
)

type state interface {
	run(*Client) (time.Duration, error)
	commandIsDisabled() bool
	handleCommand(*commandRequest, *Client) (state, error)
	handleMLMEMsg(interface{}, *Client) (state, error)
	handleMLMETimeout(*Client) (state, error)
	needTimer(*Client) (bool, time.Duration)
	timerExpired(*Client) (state, error)
}

type Command int

const (
	CmdScan Command = iota
	CmdSetScanConfig
	CmdDisconnect
)

const InfiniteTimeout = 0 * time.Second

// Start BSS

const StartBSSTimeout = 30 * time.Second

type startBSSState struct {
	running bool
}

func newStartBSSState(c *Client) *startBSSState {
	return &startBSSState{}
}

func (s *startBSSState) String() string {
	return "starting-bss"
}

func newStartBSSRequest(ssid string, beaconPeriod uint32, dtimPeriod uint32) *mlme.StartRequest {
	return &mlme.StartRequest{
		Ssid:         ssid,
		BeaconPeriod: beaconPeriod,
		DtimPeriod:   dtimPeriod,
		BssType:      mlme.BssTypes_Infrastructure,
	}
}

func (s *startBSSState) run(c *Client) (time.Duration, error) {
	req := newStartBSSRequest(c.apCfg.SSID, uint32(c.apCfg.BeaconPeriod), uint32(c.apCfg.DTIMPeriod))
	timeout := StartBSSTimeout
	if req != nil {
		if debug {
			log.Printf("start bss req: %v timeout: %v", req, timeout)
		}
		err := c.SendMessage(req, int32(mlme.Method_StartRequest))
		if err != nil {
			return 0, err
		}
		s.running = true
	}
	return timeout, nil
}

func (s *startBSSState) commandIsDisabled() bool {
	return false
}

func (s *startBSSState) handleCommand(cmd *commandRequest, c *Client) (state, error) {
	return s, nil
}

func (s *startBSSState) handleMLMEMsg(msg interface{}, c *Client) (state, error) {
	switch v := msg.(type) {
	case *mlme.StartResponse:
		if debug {
			// TODO(hahnr): Print response.
		}
		s.running = false

		// TODO(hahnr): Evaluate response.
		return s, nil

	default:
		return s, fmt.Errorf("unexpected message type: %T", v)
	}
}

func (s *startBSSState) handleMLMETimeout(c *Client) (state, error) {
	if debug {
		log.Printf("start bss timeout")
	}
	return s, nil
}

func (s *startBSSState) needTimer(c *Client) (bool, time.Duration) {
	return false, 0
}

func (s *startBSSState) timerExpired(c *Client) (state, error) {
	return s, nil
}

// Scanning

const DefaultScanInterval = 5 * time.Second
const ScanTimeout = 30 * time.Second

type scanState struct {
	pause      bool
	running    bool
	cmdPending *commandRequest
}

var twoPointFourGhzChannels = []uint16{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}
var broadcastBssid = [6]uint8{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}

func newScanState(c *Client) *scanState {
	pause := true
	if c.cfg != nil && c.cfg.SSID != "" {
		// start periodic scan.
		pause = false
	}
	return &scanState{pause: pause}
}

func (s *scanState) String() string {
	return "scanning"
}

func newScanRequest(ssid string) *mlme.ScanRequest {
	return &mlme.ScanRequest{
		BssType:        mlme.BssTypes_Infrastructure,
		Bssid:          broadcastBssid,
		Ssid:           ssid,
		ScanType:       mlme.ScanTypes_Passive,
		ChannelList:    &twoPointFourGhzChannels,
		MinChannelTime: 100,
		MaxChannelTime: 300,
	}
}

func (s *scanState) run(c *Client) (time.Duration, error) {
	var req *mlme.ScanRequest
	timeout := ScanTimeout
	if s.cmdPending != nil && s.cmdPending.id == CmdScan {
		sr := s.cmdPending.arg.(*wlan_service.ScanRequest)
		if sr.Timeout > 0 {
			timeout = time.Duration(sr.Timeout) * time.Second
		}
		req = newScanRequest("")
	} else if c.cfg != nil && c.cfg.SSID != "" && !s.pause {
		req = newScanRequest(c.cfg.SSID)
	}
	if req != nil {
		if debug {
			log.Printf("scan req: %v timeout: %v", req, timeout)
		}
		err := c.SendMessage(req, int32(mlme.Method_ScanRequest))
		if err != nil {
			return 0, err
		}
		s.running = true
	}
	return timeout, nil
}

func (s *scanState) commandIsDisabled() bool {
	return s.running
}

func (s *scanState) handleCommand(cmd *commandRequest, c *Client) (state, error) {
	switch cmd.id {
	case CmdScan:
		_, ok := cmd.arg.(*wlan_service.ScanRequest)
		if !ok {
			res := &CommandResult{}
			res.Err = &wlan_service.Error{
				wlan_service.ErrCode_InvalidArgs,
				"Invalid arguments",
			}
			cmd.respC <- res
		}
		s.cmdPending = cmd
	case CmdSetScanConfig:
		newCfg, ok := cmd.arg.(*Config)
		res := &CommandResult{}
		if !ok {
			res.Err = &wlan_service.Error{
				wlan_service.ErrCode_InvalidArgs,
				"Invalid arguments",
			}
		} else {
			c.cfg = newCfg
			if debug {
				log.Printf("New cfg: SSID %v, interval %v",
					c.cfg.SSID, c.cfg.ScanInterval)
			}
		}
		cmd.respC <- res
	default:
		cmd.respC <- &CommandResult{nil,
			&wlan_service.Error{wlan_service.ErrCode_NotSupported,
				"Can't run the command in scanState"}}
	}
	return s, nil
}

func (s *scanState) handleMLMEMsg(msg interface{}, c *Client) (state, error) {
	switch v := msg.(type) {
	case *mlme.ScanResponse:
		if debug {
			PrintScanResponse(v)
		}
		s.running = false

		if s.cmdPending != nil && s.cmdPending.id == CmdScan {
			aps := CollectScanResults(v, "", "")
			s.cmdPending.respC <- &CommandResult{aps, nil}
			s.cmdPending = nil
		} else if c.cfg != nil && c.cfg.SSID != "" {
			aps := CollectScanResults(v, c.cfg.SSID, c.cfg.BSSID)
			if len(aps) > 0 {
				c.ap = &aps[0]
				return newJoinState(), nil
			}
			s.pause = true
		}

		return s, nil

	default:
		return s, fmt.Errorf("unexpected message type: %T", v)
	}
}

func (s *scanState) handleMLMETimeout(c *Client) (state, error) {
	if debug {
		log.Printf("scan timeout")
	}
	s.pause = false
	return s, nil
}

func (s *scanState) needTimer(c *Client) (bool, time.Duration) {
	if s.running {
		return false, 0
	}
	if c.cfg == nil || c.cfg.SSID == "" {
		return false, 0
	}
	scanInterval := DefaultScanInterval
	if c.cfg != nil && c.cfg.ScanInterval > 0 {
		scanInterval = time.Duration(c.cfg.ScanInterval) * time.Second
	}
	if debug {
		log.Printf("scan pause %v start", scanInterval)
	}
	return true, scanInterval
}

func (s *scanState) timerExpired(c *Client) (state, error) {
	if debug {
		log.Printf("scan pause finished")
	}
	s.pause = false
	return s, nil
}

// Joining

type joinState struct {
}

func newJoinState() *joinState {
	return &joinState{}
}

func (s *joinState) String() string {
	return "joining"
}

func (s *joinState) run(c *Client) (time.Duration, error) {
	// Fail with error if network is an unsupported RSN.
	if c.ap.BSSDesc.Rsn != nil {
		if supported, err := eapol.IsRSNSupported(*c.ap.BSSDesc.Rsn); !supported {
			return InfiniteTimeout, err
		}
	}

	req := &mlme.JoinRequest{
		SelectedBss:        *c.ap.BSSDesc,
		JoinFailureTimeout: 20,
	}
	if debug {
		log.Printf("join req: %v", req)
	}

	return InfiniteTimeout, c.SendMessage(req, int32(mlme.Method_JoinRequest))
}

func (s *joinState) commandIsDisabled() bool {
	return true
}

func (s *joinState) handleCommand(cmd *commandRequest, c *Client) (state, error) {
	return s, nil
}

func (s *joinState) handleMLMEMsg(msg interface{}, c *Client) (state, error) {
	switch v := msg.(type) {
	case *mlme.JoinResponse:
		if debug {
			PrintJoinResponse(v)
		}

		if v.ResultCode == mlme.JoinResultCodes_Success {
			return newAuthState(), nil
		} else {
			return newScanState(c), nil
		}
	default:
		return s, fmt.Errorf("unexpected message type: %T", v)
	}
}

func (s *joinState) handleMLMETimeout(c *Client) (state, error) {
	return s, nil
}

func (s *joinState) needTimer(c *Client) (bool, time.Duration) {
	return false, 0
}

func (s *joinState) timerExpired(c *Client) (state, error) {
	return s, nil
}

// Authenticating

type authState struct {
}

func newAuthState() *authState {
	return &authState{}
}

func (s *authState) String() string {
	return "authenticating"
}

func (s *authState) run(c *Client) (time.Duration, error) {
	req := &mlme.AuthenticateRequest{
		PeerStaAddress:     c.ap.BSSDesc.Bssid,
		AuthType:           mlme.AuthenticationTypes_OpenSystem,
		AuthFailureTimeout: 20,
	}
	if debug {
		log.Printf("auth req: %v", req)
	}

	return InfiniteTimeout, c.SendMessage(req, int32(mlme.Method_AuthenticateRequest))
}

func (s *authState) commandIsDisabled() bool {
	return true
}

func (s *authState) handleCommand(cmd *commandRequest, c *Client) (state, error) {
	return s, nil
}

func (s *authState) handleMLMEMsg(msg interface{}, c *Client) (state, error) {
	switch v := msg.(type) {
	case *mlme.AuthenticateResponse:
		if debug {
			PrintAuthenticateResponse(v)
		}

		if v.ResultCode == mlme.AuthenticateResultCodes_Success {
			return newAssocState(), nil
		} else {
			return newScanState(c), nil
		}
	default:
		return s, fmt.Errorf("unexpected message type: %T", v)
	}
}

func (s *authState) handleMLMETimeout(c *Client) (state, error) {
	return s, nil
}

func (s *authState) needTimer(c *Client) (bool, time.Duration) {
	return false, 0
}

func (s *authState) timerExpired(c *Client) (state, error) {
	return s, nil
}

// Associating

type assocState struct {
}

func newAssocState() *assocState {
	return &assocState{}
}

func (s *assocState) String() string {
	return "associating"
}

func (s *assocState) run(c *Client) (time.Duration, error) {
	req := &mlme.AssociateRequest{
		PeerStaAddress: c.ap.BSSDesc.Bssid,
	}

	// If the network is an RSN, announce own cipher and authentication capabilities and configure
	// EAPOL client to process incoming EAPOL frames.
	bcnRawRSNE := c.ap.BSSDesc.Rsn
	if bcnRawRSNE != nil {
		bcnRSNE, err := elements.ParseRSN(*bcnRawRSNE)
		if err != nil {
			return InfiniteTimeout, fmt.Errorf("Error parsing Beacon RSNE")
		}

		assocRSNE := s.createAssociationRSNE(bcnRSNE)
		assocRawRSNE := assocRSNE.Bytes()
		req.Rsn = &assocRawRSNE

		supplicant := s.createSupplicant(c, bcnRSNE, assocRSNE)
		c.eapolC = s.createEAPOLClient(c, assocRSNE, supplicant)
	} else {
		c.eapolC = nil
	}

	if debug {
		log.Printf("assoc req: %v", req)
	}

	return InfiniteTimeout, c.SendMessage(req, int32(mlme.Method_AssociateRequest))
}

// Creates the RSNE used in MLME-Association.request to announce supported ciphers and AKMs to the
// Supported Ciphers and AKMs:
// AKMS: PSK
// Pairwise: CCMP-128
// Group: CCMP-128, TKIP
func (s *assocState) createAssociationRSNE(bcnRSNE *elements.RSN) *elements.RSN {
	rsne := elements.NewEmptyRSN()
	rsne.GroupData = &elements.CipherSuite{
		Type: elements.CipherSuiteType_CCMP128,
		OUI:  elements.DefaultCipherSuiteOUI,
	}
	// If GroupCipher does not support CCMP-128, fallback to TKIP.
	// Note: IEEE allows the usage of Group Ciphers which are less secure than Pairwise ones. TKIP
	// is supported for Group Ciphers solely for compatibility reasons. TKIP is considered broken
	// and will not be supported for pairwise cipher usage, not even to support compatibility with
	// older devices.
	if !bcnRSNE.GroupData.IsIn(elements.CipherSuiteType_CCMP128) {
		rsne.GroupData.Type = elements.CipherSuiteType_TKIP
	}
	rsne.PairwiseCiphers = []elements.CipherSuite{
		{
			Type: elements.CipherSuiteType_CCMP128,
			OUI:  elements.DefaultCipherSuiteOUI,
		},
	}
	rsne.AKMs = []elements.AKMSuite{
		{
			Type: elements.AkmSuiteType_PSK,
			OUI:  elements.DefaultCipherSuiteOUI,
		},
	}
	capabilities := uint16(0)
	rsne.Caps = &capabilities
	return rsne
}

func (s *assocState) createSupplicant(c *Client, bcnRSNE *elements.RSN, assocRSNE *elements.RSN) eapol.KeyExchange {
	password := ""
	if c.cfg != nil {
		password = c.cfg.Password
	}
	// TODO(hahnr): Add support for authentication selection once we support other AKMs.
	config := handshake.FourWayConfig{
		Transport:  &eapol.SMETransport{SME: c},
		PassPhrase: password,
		SSID:       c.ap.SSID,
		PeerAddr:   c.ap.BSSID,
		StaAddr:    c.staAddr,
		AssocRSNE:  assocRSNE,
		BeaconRSNE: bcnRSNE,
	}
	hs := handshake.NewFourWay(config)
	return handshake.NewSupplicant(hs)
}

func (s *assocState) createEAPOLClient(c *Client, assocRSNE *elements.RSN, keyExchange eapol.KeyExchange) *eapol.Client {
	// TODO(hahnr): Derive MIC size from AKM.
	return eapol.NewClient(eapol.Config{128, keyExchange})
}

func (s *assocState) commandIsDisabled() bool {
	return true
}

func (s *assocState) handleCommand(cmd *commandRequest, c *Client) (state, error) {
	return s, nil
}

func (s *assocState) handleMLMEMsg(msg interface{}, c *Client) (state, error) {
	switch v := msg.(type) {
	case *mlme.AssociateResponse:
		if debug {
			PrintAssociateResponse(v)
		}

		if v.ResultCode == mlme.AssociateResultCodes_Success {
			return newAssociatedState(), nil
		} else {
			return newScanState(c), nil
		}
	default:
		return s, fmt.Errorf("unexpected message type: %T", v)
	}
}

func (s *assocState) handleMLMETimeout(c *Client) (state, error) {
	return s, nil
}

func (s *assocState) needTimer(c *Client) (bool, time.Duration) {
	return false, 0
}

func (s *assocState) timerExpired(c *Client) (state, error) {
	return s, nil
}

// Associated

type associatedState struct {
}

func newAssociatedState() *associatedState {
	return &associatedState{}
}

func (s *associatedState) String() string {
	return "associated"
}

func (s *associatedState) run(c *Client) (time.Duration, error) {
	return InfiniteTimeout, nil
}

func (s *associatedState) commandIsDisabled() bool {
	// TODO(toshik): disable if Scan request is running
	return false
}

func (s *associatedState) handleCommand(cmd *commandRequest, c *Client) (state, error) {
	// TODO(toshik): handle Scan command
	switch cmd.id {
	case CmdDisconnect:
		req := &mlme.DeauthenticateRequest{
			PeerStaAddress: c.ap.BSSID,
			// TODO(hahnr): Map Reason Codes to strings and provide map.
			ReasonCode: 36, // Requesting STA is leaving the BSS
		}
		if debug {
			log.Printf("deauthenticate req: %v", req)
		}

		err := c.SendMessage(req, int32(mlme.Method_DeauthenticateRequest))
		res := &CommandResult{}
		if err != nil {
			res.Err = &wlan_service.Error{wlan_service.ErrCode_Internal, "Could not send MLME request"}
		}
		cmd.respC <- res
	default:
		cmd.respC <- &CommandResult{nil,
			&wlan_service.Error{wlan_service.ErrCode_NotSupported,
				"Can't run the command in associatedState"}}
	}
	return s, nil
}

func (s *associatedState) handleMLMEMsg(msg interface{}, c *Client) (state, error) {
	switch v := msg.(type) {
	case *mlme.DisassociateIndication:
		if debug {
			PrintDisassociateIndication(v)
		}
		return newAssocState(), nil
	case *mlme.DeauthenticateResponse:
		if debug {
			PrintDeauthenticateResponse(v)
		}
		// This was a user issued deauthentication. Clear config to prevent automatic reconnect, and
		// enter scan state.
		c.cfg = nil
		return newScanState(c), nil
	case *mlme.DeauthenticateIndication:
		if debug {
			PrintDeauthenticateIndication(v)
		}
		return newAuthState(), nil
	case *mlme_ext.SignalReportIndication:
		if debug {
			PrintSignalReportIndication(v)
		}
		return s, nil
	case *mlme_ext.EapolIndication:
		if c.eapolC != nil {
			c.eapolC.HandleEAPOLFrame(v.Data)
		}
		return s, nil
	case *mlme.EapolResponse:
		// TODO(hahnr): Evaluate response code.
		return s, nil
	default:
		return s, fmt.Errorf("unexpected message type: %T", v)
	}
}

func (s *associatedState) handleMLMETimeout(c *Client) (state, error) {
	return s, nil
}

func (s *associatedState) needTimer(c *Client) (bool, time.Duration) {
	return false, 0
}

func (s *associatedState) timerExpired(c *Client) (state, error) {
	return s, nil
}
