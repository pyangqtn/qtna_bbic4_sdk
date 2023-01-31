#!/usr/lib/cgi-bin/php-cgi

<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.0 Transitional//EN"  "http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd">
<html xmlns="http://www.w3.org/1999/xhtml" lang="en" xml:lang="en">
<head>
	<title>Quantenna Communications</title>
	<link rel="stylesheet" type="text/css" href="./themes/style.css" media="screen" />
	<link rel="stylesheet" type="text/css" href="./SpryAssets/SpryTabbedPanels.css" />
	<meta http-equiv="Content-Type" content="text/html; charset=UTF-8" />
	<meta http-equiv="expires" content="0" />
	<meta http-equiv="CACHE-CONTROL" content="no-cache" />
</head>

<script language="javascript" type="text/javascript" src="./SpryAssets/SpryTabbedPanels.js"></script>
<script language="javascript" type="text/javascript" src="./js/menu.js"></script>
<script language="javascript" type="text/javascript" src="./js/webif.js"></script>
<script type="text/javascript" src="js/qrcode.min.js"></script>
<?php
include("common.php");
$privilege = get_privilege(2);
?>


<script type="text/javascript">
var privilege="<?php echo $privilege; ?>";
</script>
<?php
$calstate = exec("get_bootval calstate");
if($calstate != 3)
{
	echo "<head>";
	echo "Wireless settings are available only in calibration state 3. Redirecting to device status page";
	echo "<meta HTTP-EQUIV=\"REFRESH\" content=\"3; url=status_device.php\">";
	echo "</head>";
	echo "</html>";
	exit();
}

$pid = exec("pidof wpa_supplicant");
if($pid == "") $pid = exec("pidof hostapd");
$val = exec("ifconfig -a | grep wifi0");
if($val == "" || $pid == "")
{
	echo "<head>";
	echo "Wireless services are not started. Redirecting to network page";
	echo "<meta HTTP-EQUIV=\"REFRESH\" content=\"3; url=status_networking.php\">";
	echo "</head>";
	exit();
}

$per_ssid_conf="/mnt/jffs2/per_ssid_config.txt";
$curr_mode=exec("call_qcsapi verify_repeater_mode");
if($curr_mode == 1)
	$curr_mode = "Repeater";
else
	$curr_mode = exec("call_qcsapi get_mode wifi0");

$curr_ap_interface = exec("call_qcsapi get_ap_interface_name");
$curr_ssid=exec("call_qcsapi get_ssid wifi0");
$curr_sta_ssid=exec("call_qcsapi get_ssid wifi0");
$curr_region="";
$curr_channel="";
$curr_sec_channel="";
$curr_channel_str="";
$curr_channel_list_20="";
$curr_channel_list_40="";
$curr_channel_list_80="";
$curr_channel_list_none="";
$curr_pmf="";
$curr_sta_pmf="";
$curr_proto="NONE";
$curr_sta_proto="NONE";
$curr_psk="";
$curr_sta_psk="";
$curr_broadcast="";
$curr_mcs=0;
$curr_priority="0";
$curr_bintval=0;
$curr_dtim_period=0;
$curr_short_gi=0;
$curr_bw="";
$curr_band="";
$curr_vlan="";
$curr_vlan_ids=array('','','','','','','','');
$rf_chipid="1";
$vht_status="";
/* Encrytion variable */
$curr_encryption="";
$curr_sta_encryption="";
/* NSS variable */
$curr_nss="";
/* Radius Server IP */
$curr_radius_ipaddr="";
/* Radius Server Port Number */
$curr_radius_port="1812";
/* Shared Key */
$curr_shared_key="";
/* SAE && OWE groups */
$curr_saegroups="";
$curr_owegroups="";
$curr_sta_saegroups="";
$curr_sta_owegroups="";
/* Group key interval */
$curr_group_key_interval="";
$curr_radius="";
$radius_arr_len="";
/* Status of HS2.0 */
$curr_hs20_status="";
/* IEEE802.11r settings */
$curr_ieee80211r="";
$curr_11r_mob_id="";
$curr_11r_nas_id="";
$curr_11r_r1kh_id="";
$curr_11r_ft_over_ds="";
$curr_11v_btm_cap="";
$curr_11k_neigh_repo="";

/* soniq */
$curr_soniq="0";
$curr_soniq_role="unconfigured";

/* DPP */
$curr_dpp_qrcode="";
$curr_sta_dpp_qrcode="";

function get_sta_proto($ssid)
{
	$is_dpp = exec("cat /mnt/jffs2/wpa_supplicant.conf|grep ctrl_interface=DIR");
	if($is_dpp != "") {
		$proto = "DPP";
		return $proto;
	}
	if($ssid == "") {
		$proto = "NONE";
		return $proto;
	}
	$ssid_esc = escape_any_characters($ssid);

	$encryption=exec("call_qcsapi SSID_get_proto wifi0 \"$ssid_esc\"");
	$authentication=exec("call_qcsapi SSID_get_authentication_mode wifi0 \"$ssid_esc\"");
	if (is_qcsapi_error($encryption) && $authentication != "OPENandOWEAuthentication")
		$proto="NONE";
	elseif ($encryption == "11i" && $authentication == "PSKAuthentication")
		$proto="11i";
	elseif ($encryption == "11i" && $authentication == "SHA256PSKAuthenticationMixed")
		$proto="11i_pmf";
	elseif ($authentication == "SAEAuthentication")
		$proto="SAE";
	elseif ($authentication == "SAEandPSKAuthentication")
		$proto="SAE-WPA-PSK";
	elseif ($authentication == "OPENandOWEAuthentication")
		$proto="OWE";
	elseif ($encryption == "WPAand11i")
		$proto="WPAand11i";
	return $proto;
}

#Input 1 is the ssid, 2 is the encryption mode NONE, 11i or WPAand11i
function set_sta_proto($ssid,$proto)
{
	if($proto == "DPP") {
		exec("sed -i 's/ctrl_interface=\/var\/run\/wpa_supplicant/ctrl_interface=DIR=\/var\/run\/wpa_supplicant/g' /mnt/jffs2/wpa_supplicant.conf");
		exec("wpa_cli reconfig > /dev/null 2>&1 &");
		//Work around of sleeping 8s to wait DPP configuration take effect.
		sleep(8);
		$curr_chan = exec("call_qcsapi get_channel wifi0");
		if($curr_chan != 36) {
			exec("call_qcsapi disassociate wifi0");
			exec("call_qcsapi set_channel wifi0 36");
		}
		return;
	}
	else {
		exec("sed -i 's/ctrl_interface=DIR=\/var\/run\/wpa_supplicant/ctrl_interface=\/var\/run\/wpa_supplicant/g' /mnt/jffs2/wpa_supplicant.conf");
		exec("wpa_cli reconfig > /dev/null 2>&1 &");
	}
	$ssid_esc = escapeshellarg(escape_any_characters($ssid));
	$proto_esc = escapeshellarg(escape_any_characters($proto));

	if ($proto == "NONE")
		exec("call_qcsapi SSID_set_authentication_mode wifi0 $ssid_esc NONE");
	elseif ($proto == "11i")
	{
		exec("call_qcsapi SSID_set_authentication_mode wifi0 $ssid_esc PSKAuthentication");
		exec("call_qcsapi SSID_set_proto wifi0 $ssid_esc $proto_esc");
		exec("call_qcsapi SSID_set_encryption_modes wifi0 $ssid_esc AESEncryption");
	}
	elseif ($proto == "11i_pmf")
	{
		exec("call_qcsapi SSID_set_authentication_mode wifi0 $ssid_esc SHA256PSKAuthenticationMixed");
		exec("call_qcsapi SSID_set_proto wifi0 $ssid_esc $proto_esc");
		exec("call_qcsapi SSID_set_encryption_modes wifi0 $ssid_esc AESEncryption");
	}
	elseif ($proto == "WPAand11i")
	{
		exec("call_qcsapi SSID_set_authentication_mode wifi0 $ssid_esc PSKAuthentication");
		exec("call_qcsapi SSID_set_proto wifi0 $ssid_esc $WPAand11i");
		exec("call_qcsapi SSID_set_encryption_modes wifi0 $ssid_esc TKIPandAESEncryption");
	}
	elseif ($proto == "SAE")
	{
		exec("call_qcsapi SSID_set_authentication_mode wifi0 $ssid_esc SAEAuthentication");
		exec("call_qcsapi SSID_set_proto wifi0 $ssid_esc $WPAand11i");
		exec("call_qcsapi SSID_set_encryption_modes wifi0 $ssid_esc AESEncryption");
	}
	elseif ($proto == "SAE-WPA-PSK")
	{
		exec("call_qcsapi SSID_set_authentication_mode wifi0 $ssid_esc SAEandPSKAuthentication");
		exec("call_qcsapi SSID_set_proto wifi0 $ssid_esc $WPAand11i");
		exec("call_qcsapi SSID_set_encryption_modes wifi0 $ssid_esc AESEncryption");
	}
	elseif ($proto == "OWE")
	{
		exec("call_qcsapi SSID_set_authentication_mode wifi0 $ssid_esc OPENandOWEAuthentication");
	}
}

function get_sae_owe_groups($curr_mode, $curr_proto, $curr_sta_proto)
{
	global $curr_owegroups, $curr_saegroups, $curr_sta_saegroups, $curr_sta_owegroups, $curr_sta_ssid, $curr_ssid;

	if ($curr_mode == "Access point" || $curr_mode == "Repeater") {
		$interface = $curr_mode == "Access point"?"wifi0":"wifi1";
		if ($curr_proto == "SAE" || $curr_proto == "SAE-WPA-PSK") {
			$curr_saegroups = trim(shell_exec("call_qcsapi get_params $interface sae_groups"));
			if(is_qcsapi_error($curr_saegroups)) {
				$curr_saegroups = "";
			}
			else {
				$curr_saegroups_arr = split(":", $curr_saegroups);
				$curr_saegroups = trim($curr_saegroups_arr[1]);
			}
			$curr_saegroups = split(" ", $curr_saegroups);
			$curr_saegroups = implode(",", $curr_saegroups);
		}

		if ($curr_proto == "OWE") {
			$curr_owegroups = trim(shell_exec("call_qcsapi get_params $interface owe_groups"));
			if(is_qcsapi_error($curr_owegroups)) {
				$curr_owegroups = "";
			}
			else {
				$curr_owegroups_arr = split(":", $curr_owegroups);
				$curr_owegroups = trim($curr_owegroups_arr[1]);
			}
			$curr_owegroups = split(" ", $curr_owegroups);
			$curr_owegroups = implode(",", $curr_owegroups);
		}
	}

	if ($curr_mode == "Station" || $curr_mode == "Repeater") {
		$sta_proto = $curr_mode == "Station"?$curr_proto:$curr_sta_proto;
		$get_ssid = $curr_mode == "Station"?$curr_ssid:$curr_sta_ssid;

		if ($sta_proto == "SAE" || $sta_proto == "SAE-WPA-PSK")
			$curr_sta_saegroups = trim(shell_exec("call_qcsapi SSID_get_params wifi0 $get_ssid sae_groups"));

		if ($sta_proto == "OWE")
			$curr_sta_owegroups = trim(shell_exec("call_qcsapi SSID_get_params wifi0 $get_ssid owe_group"));
	}
}

function set_sae_owe_groups($new_mode, $new_proto, $new_sta_proto, $new_ssid, $new_sta_ssid)
{
	global $curr_owegroups, $curr_saegroups, $curr_sta_saegroups, $curr_sta_owegroups;

	if ($new_mode == "Access point" || $new_mode == "Repeater") {
		$interface = $new_mode == "Access point"?"wifi0":"wifi1";
		$new_saegroups = $_POST['chk_saegroup'];
		$new_owegroups = $_POST['chk_owegroup'];
		if (($new_proto == "SAE" || $new_proto == "SAE-WPA-PSK") && $curr_saegroups != $new_saegroups) {
			$new_saegroups = implode(",",$new_saegroups);
			exec("call_qcsapi set_params $interface sae_groups $new_saegroups");
		}

		if ($new_proto == "OWE" && $curr_owegroups != $new_owegroups) {
			$new_owegroups = implode(",",$new_owegroups);
			exec("call_qcsapi set_params $interface owe_groups $new_owegroups");
		}
	}

	if(($new_mode == "Station" && $new_ssid != "") || ($new_mode == "Repeater" && $new_sta_ssid != "")) {
		$sta_proto = $new_mode == "Station"?$new_proto:$new_sta_proto;
		$new_sta_saegroups = $_POST['cmb_sta_saegroup'];
		$new_sta_owegroups = $_POST['cmb_sta_owegroup'];

		if (($sta_proto == "SAE" || $sta_proto == "SAE-WPA-PSK") && $new_sta_saegroups != $curr_sta_saegroups) {
			exec("call_qcsapi SSID_set_params wifi0 $curr_sta_ssid sae_groups $new_sta_saegroups");
		}

		if ($sta_proto == "OWE" && $new_sta_owegroups != $curr_sta_owegroups) {
			exec("call_qcsapi SSID_set_params wifi0 $curr_sta_ssid owe_groups $new_sta_owegroups");
		}
	}
}

function get11rEnable($device)
{
	$tmp=exec("call_qcsapi get_ieee80211r $device");
	if ($tmp == "enabled")
	{return 1;}
	else
	{return 0;}
}

function get11rMobilityDomain($device)
{
	$mobility_domain=exec("call_qcsapi get_11r_mobility_domain $device");
	if (is_qcsapi_error($mobility_domain))
	{ $mobility_domain = "";}
	return $mobility_domain;
}

function get11rNasId($device)
{
	$nas_id=exec("call_qcsapi get_11r_nas_id $device");
	if (is_qcsapi_error($nas_id))
	{ $nas_id = "";}
	return $nas_id;
}

function get11rR1khId($device)
{
	$r1kh_id=exec("call_qcsapi get_11r_r1_key_holder $device");
	if (is_qcsapi_error($r1kh_id))
	{ $r1kh_id = "";}
	return $r1kh_id;
}

function get11rFtOverDs($device)
{
	$tmp=exec("call_qcsapi get_11r_ft_over_ds $device");
	if ($tmp == "enabled")
	{return 1;}
	else
	{return 0;}
}

function get11vBtmCap($device)
{
	$tmp=exec("call_qcsapi get_btm_cap $device");
	if ($tmp == "enabled")
	{return 1;}
	else
	{return 0;}
}

function get11kNeighRepo($device)
{
	$tmp=exec("call_qcsapi get_rm_neigh_report $device");
	if ($tmp == "enabled")
	{return 1;}
	else
	{return 0;}
}

function get_value()
{
	global $per_ssid_conf,$curr_mode,$curr_ssid,$curr_channel,$curr_channel_str,$curr_channel_list_none,$curr_channel_list_20,$curr_channel_list_40,$curr_channel_list_80,$curr_pmf,$curr_sec_channel;
	global $curr_proto,$curr_psk,$curr_broadcast,$curr_mcs,$curr_priority,$curr_bintval,$curr_dtim_period,$curr_short_gi,$curr_bw,$curr_band,$curr_region, $rf_chipid,$vht_status,$curr_encryption;
	global $curr_nss,$curr_radius_ipaddr,$curr_radius_port,$curr_shared_key,$curr_group_key_interval,$curr_vlan,$curr_vlan_ids;
	global $curr_hs20_status;
	global $curr_radius,$radius_arr_len;
	global $curr_ieee80211r,$curr_11r_mob_id,$curr_11r_nas_id,$curr_11r_r1kh_id,$curr_11r_ft_over_ds;
	global $curr_11v_btm_cap;
	global $curr_11k_neigh_repo;
	global $curr_soniq,$curr_soniq_role;
	global $curr_ap_interface,$curr_sta_ssid,$curr_sta_pmf,$curr_sta_proto,$curr_sta_encryption,$curr_sta_psk;
	global $curr_dpp_qrcode,$curr_sta_dpp_qrcode;

	//Get SSID
	$curr_ssid=trim(shell_exec("call_qcsapi get_ssid wifi0"));
	//Get Configed Channel
	$curr_channel=read_wireless_conf("channel", 1);
	//Get current Secondary Channel
	$curr_sec_channel=trim(shell_exec("call_qcsapi get_sec_chan wifi0 $curr_channel"));
	//Get bw
	$curr_bw=trim(shell_exec("call_qcsapi get_bw wifi0"));
	// Get Current  band - 11ng, 11na, 11ac etc.
	$curr_band_temp = trim(shell_exec("call_qcsapi get_phy_mode wifi0"));
	$curr_band = get_band_sub_str($curr_band_temp);

	//Get Current Channel
	$curr_auto_chan=trim(shell_exec("call_qcsapi get_channel wifi0"));
	if (is_qcsapi_error($curr_auto_chan))
		$curr_auto_chan="";

	$curr_scan_status = exec("iwpriv wifi0 get_scanstatus");
	$arr = explode(':', $curr_scan_status);
	$curr_scan_status = $arr[1];

	if ($curr_scan_status == "1")
		$curr_channel_str="Scanning:".$curr_auto_chan;
	else
		$curr_channel_str="Current Channel:".$curr_auto_chan;
	// Get RF Chip ID
	$rf_chipid = trim(shell_exec("call_qcsapi get_board_parameter rf_chipid"));
	// Get Vht status
	$vht_status = trim(shell_exec("call_qcsapi get_board_parameter vht_status"));

	//Get Channel List
	$curr_region=trim(shell_exec("call_qcsapi get_regulatory_region wifi0"));
	if($curr_region != "none")
	{
		$curr_channel_list_20 = trim(shell_exec("call_qcsapi get_list_regulatory_channels $curr_region 20"));
		$curr_channel_list_40 = trim(shell_exec("call_qcsapi get_list_regulatory_channels $curr_region 40"));
		$curr_channel_list_80 = trim(shell_exec("call_qcsapi get_list_regulatory_channels $curr_region 80"));
	}
	else {
		//$curr_channel_list_none = trim(shell_exec("call_qcsapi get_channel_list wifi0"));
		/* ToDo: Need to debug the get_channel_list command */
		/* Dual band channel list. populate_channellist will filter channels based on band */
		$curr_channel_list_none = "1,2,3,4,5,6,7,8,9,10,11,12,13,36,40,44,48,52,56,60,64,100,104,108,112,116,120,124,128,132,136,149,153,157,161,165";
	}

	//Get MCS
	$tmp=trim(shell_exec("call_qcsapi get_option wifi0 autorate"));;
	if ($tmp == "TRUE")
		$curr_mcs=0;
	else
		$curr_mcs=trim(shell_exec("call_qcsapi get_mcs_rate wifi0"));
	//Get nss and mcs in 11ac band
	if ($curr_band == "11ac" || $curr_band == "11acOnly")
	{
		$curr_nss = 0;
		if ($curr_mcs != "0")
		{
			//curr_mcs format : mcs401 - nss = 4 and mcs = 1
			$curr_nss = substr($curr_mcs, 3, 1);
			$curr_mcs = substr($curr_mcs, 5, 1);
		}
	}
	//==========================================================================================================
	if ($curr_mode == "Access point")
	{
		$curr_hs20_status=trim(shell_exec("call_qcsapi get_hs20_status wifi0"));
		//Get PMF
		$curr_pmf=trim(shell_exec("call_qcsapi get_pmf wifi0"));
		//Get Current Proto and PSK
		$curr_proto = get_ap_proto();
		if ($curr_proto != "NONE" && $curr_proto != "OWE" && $curr_proto != "DPP")
		{
			$curr_psk = trim(shell_exec("call_qcsapi get_passphrase wifi0 0"));
			if (is_qcsapi_error($curr_psk))
				$curr_psk = trim(shell_exec("call_qcsapi get_pre_shared_key wifi0 0"));
			$curr_group_key_interval = trim(shell_exec("call_qcsapi get_group_key_interval wifi0"));
		}
		if ($curr_proto == "WPA2-EAP" || $curr_proto == "WPAand11i-EAP")
		{
			$curr_radius = trim(shell_exec("call_qcsapi get_radius_auth_server_cfg wifi0"));
			if (is_qcsapi_error($curr_radius))
			{
				 $radius_arr_len=0;
			}
			else
			{
				$curr_radius=explode("\n",$curr_radius);
				$radius_arr_len = count($curr_radius);
			}
		}
		if($curr_proto == "DPP") {
			$mac = exec("call_qcsapi get_mac_addr wifi0");
			exec("call_qcsapi set_channel wifi0 36");
			$dpp_index = exec("call_qcsapi dpp_param wifi0 dpp_bootstrap_gen type qrcode curve P-256 mac $mac");
			$curr_dpp_qrcode = exec("call_qcsapi dpp_param wifi0 dpp_bootstrap_get_uri local $dpp_index");
		}
		else {
			$curr_dpp_qrcode = "";
		}
		//Get Broadcast SSID
		$curr_broadcast = trim(shell_exec("call_qcsapi get_option wifi0 broadcast_SSID"));
		//Get Priority
		$curr_priority = trim(shell_exec("call_qcsapi get_priority wifi0"));
		//Get Beacon Interval
		$curr_bintval=trim(exec("call_qcsapi get_beacon_interval wifi0"));
		//Get DTIM Period
		$curr_dtim_period=trim(exec("call_qcsapi get_dtim wifi0"));
		//Get Short GI
		$curr_short_gi=trim(exec("call_qcsapi get_option wifi0 short_gi"));
		//Get VLAN
		$curr_vlan=exec("cat $per_ssid_conf | grep wifi0 | awk -F 'vlan=' '{print $2}' | awk -F '&' '{print $1}'");
		//Get all the current VLAN
		$curr_vlan_ids[0]=$curr_vlan;
		$curr_vlan_ids[1]=exec("cat $per_ssid_conf | grep wifi1 | awk -F 'vlan=' '{print $2}' | awk -F '&' '{print $1}'");
		$curr_vlan_ids[2]=exec("cat $per_ssid_conf | grep wifi2 | awk -F 'vlan=' '{print $2}' | awk -F '&' '{print $1}'");
		$curr_vlan_ids[3]=exec("cat $per_ssid_conf | grep wifi3 | awk -F 'vlan=' '{print $2}' | awk -F '&' '{print $1}'");
		$curr_vlan_ids[4]=exec("cat $per_ssid_conf | grep wifi4 | awk -F 'vlan=' '{print $2}' | awk -F '&' '{print $1}'");
		$curr_vlan_ids[5]=exec("cat $per_ssid_conf | grep wifi5 | awk -F 'vlan=' '{print $2}' | awk -F '&' '{print $1}'");
		$curr_vlan_ids[6]=exec("cat $per_ssid_conf | grep wifi6 | awk -F 'vlan=' '{print $2}' | awk -F '&' '{print $1}'");
		$curr_vlan_ids[7]=exec("cat $per_ssid_conf | grep wifi7 | awk -F 'vlan=' '{print $2}' | awk -F '&' '{print $1}'");
		//Get ieee80211r settings
		$curr_ieee80211r=get11rEnable("wifi0");
		$curr_11r_mob_id=get11rMobilityDomain("wifi0");
		$curr_11r_nas_id=get11rNasId("wifi0");
		$curr_11r_r1kh_id=get11rR1khId("wifi0");
		$curr_11r_ft_over_ds=get11rFtOverDs("wifi0");
		$curr_11v_btm_cap=get11vBtmCap("wifi0");
		$curr_11k_neigh_repo=get11kNeighRepo("wifi0");
	}
	else if ($curr_mode == "Station")
	{
		//Get PMF
		$curr_ssid_esc = escape_any_characters($curr_ssid);
		if($curr_ssid_esc == "")
		{
			$curr_pmf="0";
		}
		else
		{
			$curr_pmf=trim(shell_exec("call_qcsapi SSID_get_pmf wifi0 \"$curr_ssid_esc\""));
			if (is_qcsapi_error($curr_pmf)) {
				$curr_pmf="0";
			}
		}
		$curr_proto = get_sta_proto($curr_ssid);
		if($curr_proto == "DPP") {
			$mac = exec("call_qcsapi get_mac_addr wifi0");
			$curr_chan = exec("call_qcsapi get_channel wifi0");
			if($curr_chan != 36) {
				exec("call_qcsapi disassociate wifi0");
				exec("call_qcsapi set_channel wifi0 36");
			}
			$dpp_index = exec("call_qcsapi dpp_param wifi0 dpp_bootstrap_gen type qrcode curve P-256 mac $mac");
			$curr_dpp_qrcode = exec("call_qcsapi dpp_param wifi0 dpp_bootstrap_get_uri local $dpp_index");
			exec("call_qcsapi dpp_param wifi0 dpp_listen freq 5180 role enrollee");
		}
		else {
			$curr_dpp_qrcode = "";
			if ($curr_proto != "NONE" && $curr_proto != "OWE")
			{
				/* Get Encrytion method */
				$curr_encryption = trim(shell_exec("call_qcsapi SSID_get_encryption_modes wifi0 \"$curr_ssid_esc\""));
				$curr_psk = trim(shell_exec("call_qcsapi SSID_get_key_passphrase wifi0 \"$curr_ssid_esc\" 0"));
				if($curr_psk == "")
					$curr_psk = trim(shell_exec("call_qcsapi SSID_get_pre_shared_key wifi0 \"$curr_ssid_esc\" 0"));
			}
		}
	}
	else if($curr_mode == "Repeater")
	{
		$curr_ssid=trim(shell_exec("call_qcsapi get_ssid $curr_ap_interface"));
		$curr_sta_ssid=trim(shell_exec("call_qcsapi get_ssid wifi0"));
		$curr_hs20_status=trim(shell_exec("call_qcsapi get_hs20_status $curr_ap_interface"));
		//Get PMF
		$curr_pmf=trim(shell_exec("call_qcsapi get_pmf $curr_ap_interface"));
		//Get STA PMF
		$curr_sta_ssid_esc = escape_any_characters($curr_sta_ssid);
		if($curr_sta_ssid_esc == "")
		{
			$curr_sta_pmf="0";
		}
		else
		{
			$curr_sta_pmf=trim(shell_exec("call_qcsapi SSID_get_pmf wifi0 \"$curr_sta_ssid_esc\""));
			if (is_qcsapi_error($curr_sta_pmf)) {
				$curr_sta_pmf="0";
			}
		}
		//Get Current Proto and PSK
		$curr_proto = get_ap_proto();
		if ($curr_proto != "NONE" && $curr_proto != "OWE" && $curr_proto != "DPP")
		{
			$curr_psk = trim(shell_exec("call_qcsapi get_passphrase $curr_ap_interface 0"));
			if (is_qcsapi_error($curr_psk))
				$curr_psk = trim(shell_exec("call_qcsapi get_pre_shared_key $curr_ap_interface 0"));
			$curr_group_key_interval = trim(shell_exec("call_qcsapi get_group_key_interval $curr_ap_interface"));
		}
		if ($curr_proto == "WPA2-EAP" || $curr_proto == "WPAand11i-EAP")
		{
			$curr_radius = trim(shell_exec("call_qcsapi get_radius_auth_server_cfg $curr_ap_interface"));
			if (is_qcsapi_error($curr_radius))
			{
				 $radius_arr_len=0;
			}
			else
			{
				$curr_radius=explode("\n",$curr_radius);
				$radius_arr_len = count($curr_radius);
			}
		}
		if($curr_proto == "DPP") {
			$mac = exec("call_qcsapi get_mac_addr wifi1");
			exec("call_qcsapi disassociate wifi0");
			exec("call_qcsapi set_channel wifi0 36");
			$dpp_index = exec("call_qcsapi dpp_param wifi1 dpp_bootstrap_gen type qrcode curve P-256 mac $mac");
			$curr_dpp_qrcode = exec("call_qcsapi dpp_param wifi1 dpp_bootstrap_get_uri local $dpp_index");
		}
		else {
			$curr_dpp_qrcode = "";
		}
		$curr_sta_proto = get_sta_proto($curr_sta_ssid);
		if($curr_sta_proto == "DPP") {
			$curr_chan = exec("call_qcsapi get_channel wifi0");
			if($curr_chan != 36) {
				exec("call_qcsapi disassociate wifi0");
				exec("call_qcsapi set_channel wifi0 36");
			}
			$sta_mac = exec("call_qcsapi get_mac_addr wifi0");
			$sta_dpp_index = exec("call_qcsapi dpp_param wifi0 dpp_bootstrap_gen type qrcode curve P-256 mac $sta_mac");
			$curr_sta_dpp_qrcode = exec("call_qcsapi dpp_param wifi0 dpp_bootstrap_get_uri local $sta_dpp_index");
			exec("call_qcsapi dpp_param wifi0 dpp_listen freq 5180 role enrollee");
		}
		else {
			$curr_sta_dpp_qrcode = "";
			$curr_sta_ssid_esc = escape_any_characters($curr_sta_ssid);
			if ($curr_sta_proto != "NONE" && $curr_sta_proto != "OWE")
			{
				/* Get Encrytion method */
				$curr_sta_encryption = trim(shell_exec("call_qcsapi SSID_get_encryption_modes wifi0 \"$curr_sta_ssid_esc\""));
				$curr_sta_psk = trim(shell_exec("call_qcsapi SSID_get_key_passphrase wifi0 \"$curr_sta_ssid_esc\" 0"));
				if($curr_sta_psk == "")
					$curr_sta_psk = trim(shell_exec("call_qcsapi SSID_get_pre_shared_key wifi0 \"$curr_sta_ssid_esc\" 0"));
			}
		}
		//Get Broadcast SSID
		$curr_broadcast = trim(shell_exec("call_qcsapi get_option $curr_ap_interface broadcast_SSID"));
		//Get Priority
		$curr_priority = trim(shell_exec("call_qcsapi get_priority $curr_ap_interface"));
		//Get Beacon Interval
		$curr_bintval=trim(exec("call_qcsapi get_beacon_interval $curr_ap_interface"));
		//Get DTIM Period
		$curr_dtim_period=trim(exec("call_qcsapi get_dtim $curr_ap_interface"));
		//Get Short GI
		$curr_short_gi=trim(exec("call_qcsapi get_option wifi0 short_gi"));
		//Get VLAN
		$curr_vlan=exec("call_qcsapi show_vlan_config $curr_ap_interface | grep VLAN | awk -F 'VLAN ' '{print $2}'");
		if($curr_vlan == "disabled")
			$curr_vlan="";
		//Get all the current VLAN
		$curr_vlan_ids[0]=$curr_vlan;
		$curr_vlan_ids[1]=exec("call_qcsapi show_vlan_config wifi1 | grep VLAN | awk -F 'VLAN ' '{print $2}'");
		if($curr_vlan_ids[1] == "disabled" || $curr_ap_interface =="wifi1")
			$curr_vlan_ids[1]="";
		$curr_vlan_ids[2]=exec("call_qcsapi show_vlan_config wifi2 | grep VLAN | awk -F 'VLAN ' '{print $2}'");
		if($curr_vlan_ids[2] == "disabled" || $curr_ap_interface =="wifi2")
			$curr_vlan_ids[2]="";
		$curr_vlan_ids[3]=exec("call_qcsapi show_vlan_config wifi3 | grep VLAN | awk -F 'VLAN ' '{print $2}'");
		if($curr_vlan_ids[3] == "disabled" || $curr_ap_interface =="wifi3")
			$curr_vlan_ids[3]="";
		$curr_vlan_ids[4]=exec("call_qcsapi show_vlan_config wifi4 | grep VLAN | awk -F 'VLAN ' '{print $2}'");
		if($curr_vlan_ids[4] == "disabled" || $curr_ap_interface =="wifi4")
			$curr_vlan_ids[4]="";
		$curr_vlan_ids[5]=exec("call_qcsapi show_vlan_config wifi5 | grep VLAN | awk -F 'VLAN ' '{print $2}'");
		if($curr_vlan_ids[5] == "disabled" || $curr_ap_interface =="wifi5")
			$curr_vlan_ids[5]="";
		$curr_vlan_ids[6]=exec("call_qcsapi show_vlan_config wifi6 | grep VLAN | awk -F 'VLAN ' '{print $2}'");
		if($curr_vlan_ids[6] == "disabled" || $curr_ap_interface =="wifi6")
			$curr_vlan_ids[6]="";
		$curr_vlan_ids[7]=exec("call_qcsapi show_vlan_config wifi7 | grep VLAN | awk -F 'VLAN ' '{print $2}'");
		if($curr_vlan_ids[7] == "disabled" || $curr_ap_interface =="wifi7")
			$curr_vlan_ids[7]="";
		//Get ieee80211r settings
		$curr_ieee80211r=get11rEnable($curr_ap_interface);
		$curr_11r_mob_id=get11rMobilityDomain($curr_ap_interface);
		$curr_11r_nas_id=get11rNasId($curr_ap_interface);
		$curr_11r_r1kh_id=get11rR1khId($curr_ap_interface);
		$curr_11r_ft_over_ds=get11rFtOverDs($curr_ap_interface);
		$curr_11v_btm_cap=get11vBtmCap($curr_ap_interface);
		$curr_11k_neigh_repo=get11kNeighRepo($curr_ap_interface);
	}

	get_sae_owe_groups($curr_mode, $curr_proto, $curr_sta_proto);

	//Get SONiQ enable/disable value
	$curr_soniq=read_wireless_conf("qsteer", 1);
	if ($curr_soniq == "")
		$curr_soniq="1";
	$pid = exec("pidof csmd");
	if($pid != "")
		$curr_soniq_role=exec("qcomm_cli show_role | head -n 1 | awk '{print $2}'");
}

function set_value()
{
	global $per_ssid_conf,$curr_mode,$curr_ssid,$curr_channel,$curr_channel_str,$curr_channel_list_none,
		$curr_channel_list_20,$curr_channel_list_40,$curr_channel_list_80,$curr_pmf,$curr_proto,$curr_psk,
		$curr_broadcast,$curr_mcs,$curr_priority,$curr_bintval,$curr_dtim_period,$curr_short_gi,$curr_bw,$curr_band,
		$curr_region,$curr_vlan,$curr_group_key_interval;
	global $curr_radius_ipaddr,$curr_radius_port,$curr_shared_key,$curr_nss;
	global $curr_ieee80211r, $curr_11r_mob_id,$curr_11r_nas_id,$curr_11r_r1kh_id,$curr_11r_ft_over_ds;
	global $curr_11v_btm_cap;
	global $curr_11k_neigh_repo;
	global $curr_soniq,$curr_soniq_role;
	global $curr_ap_interface,$curr_sta_ssid,$curr_sta_pmf,$curr_sta_proto,$curr_sta_encryption,$curr_sta_psk;

	$need_reboot=0;
	$modify_flag=0;
	//Save
	if ($_POST['action'] == 0)
	{
		$new_mode=$_POST['cmb_devicemode'];
		$new_nss=$_POST['cmb_nss'];
		$new_mcs=$_POST['cmb_txrate'];
		$new_soniq=$_POST['chb_soniq'];
		if ($new_mode != $curr_mode)
		{
			if($new_mode == "Station")
				$s_new_mode = "sta";
			else if($new_mode == "Access point")
				$s_new_mode = "ap";
			else if($new_mode == "Repeater")
				$s_new_mode = "repeater";
			write_wireless_conf("mode",$s_new_mode);
			$_SESSION['qtn_can_reboot']=TRUE;
			header('Location: system_rebooted.php');
			exit();
		}
		else
		{
			$new_ssid=$_POST['txt_essid'];
			$new_pmf=$_POST['cmb_pmf'];
			$new_proto=$_POST['cmb_encryption'];
			$new_psk=$_POST['txt_passphrase'];
			$new_radius_ipaddr=$_POST['txt_radius_ipaddr'];
			$new_radius_port=$_POST['txt_radius_port'];
			$new_shared_key=$_POST['txt_shared_key'];
			$new_group_key_interval=$_POST['txt_group_key_interval'];
			$new_is_psk=$_POST['is_psk'];
			$new_bw=$_POST['cmb_bandwidth'];
			$new_band=$_POST['cmb_wirelessmode'];

			if ($curr_mode == "Access point")
			{
				$new_channel=$_POST['cmb_channel'];
				$new_sec_channel=$_POST['cmb_sec_channel'];
				$new_broadcast=$_POST['chk_broadcast'];
				$new_broadcast=($new_broadcast == "1")? "TRUE" : "FALSE";
				$new_wpsstate=$_POST['cmb_wpsstate'];
				$new_priority=$_POST['cmb_priority'];
				$new_bintval=$_POST['txt_beaconinterval'];
				$new_dtim_period=$_POST['txt_dtimperiod'];
				$new_short_gi=$_POST['chb_shortgi'];
				$new_vlan=$_POST['txt_vlan'];
				$new_short_gi=($new_short_gi == "1")? "TRUE" : "FALSE";
				$radius_count = $_POST['NumRowsRadius'];
				$chkbox_arr = $_POST['chk_box_radius'];
				$chkbox_arr = explode(",", $chkbox_arr);
				$new_ieee80211r = $_POST['chk_ieee80211r'];
				$new_ieee80211r = ($new_ieee80211r == "1") ? 1 : 0;
				$new_11r_mob_id = $_POST['txt_mobility_domain'];
				$new_11r_nas_id = $_POST['txt_nas_id'];
				$new_11r_r1kh_id = $_POST['txt_r1kh_id'];
				$new_11r_ft_over_ds = $_POST['chk_ft_over_ds'];
				$new_11r_ft_over_ds = ($new_11r_ft_over_ds == "1") ? 1 : 0;
				$new_11v_btm_cap = $_POST['chk_11v_btm_cap'];
				$new_11v_btm_cap = ($new_11v_btm_cap == "1") ? 1 : 0;
				$new_11k_neigh_repo = $_POST['chk_11k_neigh_repo'];
				$new_11k_neigh_repo = ($new_11k_neigh_repo == "1") ? 1 : 0;

				$add_radius = $_POST['add_radius'];
				$del_radius = $_POST['del_radius'];

				//DELETE
				if ($del_radius == 1) {
					for($i = 0; $i <= $radius_count; $i++) {
						if ($chkbox_arr[$i] == "true")
						{
								$del_val = $_POST['txtbox_radius'][$i];
								$del_val = explode(" ", $del_val);
								$del_val_0_esc = escapeshellarg($del_val[0]);
								$del_val_1_esc = escapeshellarg($del_val[1]);
								exec("call_qcsapi del_radius_auth_server_cfg wifi0 $del_val_0_esc $del_val_1_esc");
						}
					}
				}

				//ADD
				if ($add_radius == 1) {
					for($i = 0; $i < $radius_count; $i++) {
						$add_val = $_POST['txtbox_radius'][$i];
						$add_val = explode(",", $add_val);
						$add_val0 = explode(" ",$add_val[0]);
						$add_val0_esc0 = escapeshellarg($add_val0[0]);
						$add_val0_esc1 = escapeshellarg($add_val0[1]);
						$add_val0_esc2 = escapeshellarg($add_val0[2]);
						exec("call_qcsapi add_radius_auth_server_cfg wifi0 $add_val0_esc0 $add_val0_esc1 $add_val0_esc2");
					}
				}

				//Set SSID;
				if ($new_ssid != $curr_ssid)
				{
					if ($curr_soniq_role == "master")
					{
						$new_ssid = escape_char_for_qwebapi($new_ssid);
						exec("qweb set_inactive Device.WiFi.SSID.{0}.SSID \"\\\"$new_ssid\\\"\"");
					}
					else
					{
						$new_ssid = escape_any_characters($new_ssid);
						exec("call_qcsapi set_ssid wifi0 \"$new_ssid\"");
					}
				}
				//set band
				if ($new_band!=$curr_band)
				{
					$new_band_esc=escapeshellarg($new_band);
					exec("iwpriv wifi0 mode $new_band_esc");
					write_wireless_conf("band",$new_band);
					$need_reboot=1;
				}
				//set bw
				if ($new_bw!=$curr_bw)
				{
					$new_bw_esc=escapeshellarg($new_bw);
					exec("call_qcsapi set_bw wifi0 $new_bw_esc");
					write_wireless_conf("bw",$new_bw);
				}
				//Set Channel
				if ($new_channel != $curr_channel)
				{
					$new_channel_esc=escapeshellarg($new_channel);
					if ($curr_region == "none")
					{
						exec("call_qcsapi set_channel wifi0 $new_channel_esc");
					}
					else
					{
						$curr_region_esc=escapeshellarg($curr_region);
						exec("call_qcsapi set_regulatory_channel wifi0 $new_channel_esc $curr_region_esc 0");
					}

					write_wireless_conf("channel",$new_channel);
					sleep(1);
				}
				//Set Secondary channel
				if ($new_sec_channel!=$curr_sec_channel)
				{
					if ($new_sec_channel > $curr_channel){
						$curr_channel_esc=escapeshellarg($curr_channel);
						exec("call_qcsapi set_sec_chan wifi0 $curr_channel_esc 0");
					}
					else{
						$curr_channel_esc=escapeshellarg($curr_channel);
						exec("call_qcsapi set_sec_chan wifi0 $curr_channel_esc 1");
					}
				}
				//Set PMF
				if ($new_pmf!=$curr_pmf)
				{
					$new_pmf_esc=escapeshellarg($new_pmf);
					exec("call_qcsapi update_persistent_param wifi0 pmf $new_pmf_esc");
					exec("call_qcsapi set_pmf wifi0 $new_pmf_esc");
				}
				//Set Protocol
				if ($new_proto != $curr_proto)
				{
					if ($curr_soniq_role == "master")
					{
						if ($new_proto == "NONE")
						{
							exec("qweb set_inactive Device.WiFi.AccessPoint.{0}.Security.ModeEnabled \\\"None\\\"");
						}
						else if ($new_proto == "11i")
						{
							exec("qweb set_inactive Device.WiFi.AccessPoint.{0}.Security.ModeEnabled \\\"WPA2-Personal\\\"");
						}
					}
					else
					{
						set_ap_proto($new_proto);
					}
				}
				// DPP configurator behavior
				if($new_proto == "DPP") {
					$new_dpp_role = $_POST['cmb_dpp_role'];
					$new_dpp_enrollee = $_POST['cmb_dpp_enrollee'];
					$new_dpp_qrcode = $_POST['txt_dpp_qrcode'];
					if($new_dpp_role == "enrollee") {
						exec("call_qcsapi set_channel wifi0 36");
					}
					else {
						if($new_dpp_qrcode != "") {
							$new_ssid_esc = escape_any_characters($new_ssid);
							exec("call_qcsapi set_channel wifi0 36");
							$dpp_configurator_peer_id = exec("call_qcsapi dpp_param wifi0 dpp_configurator_add curve P-256");
							$dpp_enrollee_peer_id = exec("call_qcsapi dpp_param wifi0 dpp_qr_code uri '$new_dpp_qrcode'");sleep(1);
							if($new_dpp_enrollee == "sta") {
								exec("call_qcsapi dpp_param wifi0 dpp_auth_init peer $dpp_enrollee_peer_id role configurator conf sta-dpp SSID $new_ssid_esc configurator $dpp_configurator_peer_id");
							}
							else {
								exec("call_qcsapi dpp_param wifi0 dpp_auth_init peer $dpp_enrollee_peer_id role configurator conf ap-dpp SSID $new_ssid_esc configurator $dpp_configurator_peer_id");
							}
						}
					}
				}
				//Set Passphrase
				if (strcmp($new_psk, $curr_psk) != 0 && $new_proto != "DPP")
				{
					if ($curr_soniq_role == "master")
					{
						$new_psk = escape_char_for_qwebapi($new_psk);
						if($new_is_psk == 1)
							exec("qweb set_inactive Device.WiFi.AccessPoint.{0}.Security.PreSharedKey \"\\\"$new_psk\\\"\"");
						else
							exec("qweb set_inactive Device.WiFi.AccessPoint.{0}.Security.KeyPassphrase \"\\\"$new_psk\\\"\"");
					}
					else
					{
						$new_psk = escape_any_characters($new_psk);
						if($new_is_psk == 1)
							exec("call_qcsapi set_pre_shared_key wifi0 0 \"$new_psk\"");
						else
							exec("call_qcsapi set_passphrase wifi0 0 \"$new_psk\";");
					}
				}
				//set group key interval
				if ($new_proto == "11i" || $new_proto == "WPAand11i" || $new_proto == "WPA2-EAP" || $new_proto == "11i_pmf"
					|| $new_proto == "SAE" || $new_proto == "SAE-WPA-PSK")
				{
					if ($new_group_key_interval != $curr_group_key_interval)
					{
						$new_group_key_interval_esc = escape_any_characters($new_group_key_interval);
						exec("call_qcsapi set_group_key_interval wifi0 \"$new_group_key_interval_esc\"");
					}
				}
				if ($new_proto == "WPA2-EAP" || $new_proto == "WPAand11i-EAP")
				{
					//Set EAP radius ipaddr
					if ($new_radius_ipaddr != $curr_radius_ipaddr)
					{
						$new_radius_ipaddr_esc = escape_any_characters($new_radius_ipaddr);
						exec("call_qcsapi set_eap_radius_ipaddr wifi0 \"$new_radius_ipaddr_esc\"");
					}
					//Set EAP radius port
					if (($new_radius_port != $curr_radius_port) || ($new_radius_port == "1812"))
					{
						$new_radius_port_esc = escape_any_characters($new_radius_port);
						exec("call_qcsapi set_eap_radius_port wifi0 \"$new_radius_port_esc\"");
					}
					//Set EAP shared secret
					if ($new_shared_key != $curr_shared_key)
					{
						$new_shared_key_esc = escape_any_characters($new_shared_key);
						exec("call_qcsapi set_eap_shared_key wifi0 \"$new_shared_key_esc\"");
					}
				}
				//Set Broadcast SSID
				if ($new_broadcast != $curr_broadcast){
					$new_broadcast_esc=escapeshellarg($new_broadcast);
					exec("call_qcsapi set_option wifi0 broadcast_SSID $new_broadcast_esc");
				}
				//Set Priority
				if($curr_priority != $new_priority)
				{
					$new_priority_esc=escapeshellarg($new_priority);
					exec("call_qcsapi set_priority wifi0 $new_priority_esc");
					$modify_flag=1;
				}
				//Set Beacon Interval
				if ($new_bintval != $curr_bintval)
				{
					$new_bintval_esc=escapeshellarg($new_bintval);
					exec("call_qcsapi set_beacon_interval wifi0 $new_bintval_esc");
				}
				//Set DTIM Period
				if ($new_dtim_period != $curr_dtim_period)
				{
					$new_dtim_period_esc=escapeshellarg($new_dtim_period);
					exec("call_qcsapi set_dtim wifi0 $new_dtim_period_esc");
				}
				//Set Short GI
				if ($new_short_gi != $curr_short_gi)
				{
					$new_short_gi_esc=escapeshellarg($new_short_gi);
					exec("call_qcsapi set_option wifi0 short_gi $new_short_gi_esc");
				}
				//Set VLAN
				if ($curr_vlan!=$new_vlan)
				{
					$modify_flag=1;
					$new_vlan_esc=escapeshellarg($new_vlan);
					$curr_vlan_esc=escapeshellarg($new_vlan);

					//Remove vlan tag for the device
					if ($new_vlan=="")
					{
						exec("call_qcsapi vlan_config wifi0 unbind $curr_vlan_esc");
						exec("call_qcsapi vlan_config wifi0 disable");
					}
					//Change a vlan tag for the device
					else
					{
						if($curr_vlan=="")
						{
							exec("call_qcsapi vlan_config wifi0 enable");
							exec("call_qcsapi vlan_config wifi0 bind $new_vlan_esc");
						}
						else
						{
							exec("call_qcsapi vlan_config wifi0 unbind $curr_vlan_esc");
							exec("call_qcsapi vlan_config wifi0 bind $new_vlan_esc");
						}
					}
				}
				//Change ieee80211r setting
				if ($new_ieee80211r != $curr_ieee80211r)
				{
					$new_ieee80211r_esc = escapeshellarg($new_ieee80211r);
					exec("call_qcsapi set_ieee80211r wifi0 $new_ieee80211r_esc");
				}

				if($new_ieee80211r == "1")
				{
					if ($new_11r_mob_id != $curr_11r_mob_id)
					{
						$new_11r_mob_id_esc = escapeshellarg($new_11r_mob_id);
						exec("call_qcsapi set_11r_mobility_domain wifi0 $new_11r_mob_id_esc");
					}
					if ($new_11r_nas_id != $curr_11r_nas_id)
					{
						$new_11r_nas_id_esc = escapeshellarg($new_11r_nas_id);
						exec("call_qcsapi set_11r_nas_id wifi0 $new_11r_nas_id_esc");
					}
					if ($new_11r_r1kh_id != $curr_11r_r1kh_id)
					{
						$new_11r_r1kh_id_esc = escapeshellarg($new_11r_r1kh_id);
						if ($new_11r_r1kh_id_esc == "")
							$new_11r_r1kh_id_esc = 0;
						exec("call_qcsapi set_11r_r1_key_holder wifi0 $new_11r_r1kh_id_esc");
					}
					$new_11r_ft_over_ds_esc = escapeshellarg($new_11r_ft_over_ds);
					exec("call_qcsapi set_11r_ft_over_ds wifi0 $new_11r_ft_over_ds_esc");
				}
				/* Change ieee80211v setting */
				if ($new_11v_btm_cap != $curr_11v_btm_cap)
				{
					write_wireless_conf("btm_cap",$new_11v_btm_cap);
					$new_11v_btm_cap_esc = escapeshellarg($new_11v_btm_cap);
					exec("call_qcsapi set_btm_cap wifi0 $new_11v_btm_cap_esc");
				}
				/* Change ieee80211k setting */
				if ($new_11k_neigh_repo != $curr_11k_neigh_repo)
				{
					write_wireless_conf("neigh_repo",$new_11k_neigh_repo);
					$new_11k_neigh_repo_esc = escapeshellarg($new_11k_neigh_repo);
					exec("call_qcsapi set_rm_neigh_report wifi0 $new_11k_neigh_repo_esc");
				}

				if($modify_flag == 1)
				{
					$old_line=exec("cat $per_ssid_conf | grep wifi0");
					$new_line="wifi0:priority=$new_priority";
					if ($new_vlan != "")
						$new_line="wifi0:priority=$new_priority&vlan=$new_vlan";

					$per_ssid_content=file_get_contents($per_ssid_conf);
					$per_ssid_content=str_replace($old_line,$new_line,$per_ssid_content);
					$tmp=file_put_contents($per_ssid_conf, $per_ssid_content);
				}
			}
			else if ($curr_mode == "Station")
			{
				$new_ssid_esc = escapeshellarg(escape_any_characters($new_ssid));

				if ($new_proto == "DPP") {
					//Set Protocol
					set_sta_proto($new_ssid,$new_proto);
				}
				else {
					//Set SSID
					if ($new_ssid != $curr_ssid)
					{
						$tmp=exec("call_qcsapi verify_ssid wifi0 $new_ssid_esc");
						if (is_qcsapi_error($tmp))
						{
							exec("call_qcsapi create_ssid wifi0 $new_ssid_esc");
						}
						//Set PMF
						$new_pmf_esc=escapeshellarg($new_pmf);
						$new_ssid_esc = escapeshellarg(escape_any_characters($new_ssid));
						exec("call_qcsapi SSID_set_pmf wifi0 $new_ssid_esc $new_pmf_esc");

						//Set Protocol
						set_sta_proto($new_ssid,$new_proto);

						//Set Passphrase
						$new_ssid_esc = escapeshellarg(escape_any_characters($new_ssid));
						$new_psk_esc = escapeshellarg(escape_any_characters($new_psk));
						if($new_is_psk == 1)
							exec("call_qcsapi SSID_set_pre_shared_key wifi0 $new_ssid_esc 0 $new_psk_esc");
						else
							exec("call_qcsapi SSID_set_key_passphrase wifi0 $new_ssid_esc 0 $new_psk_esc;");
					}
					else
					{
						//Set PMF
						if ($new_pmf!=$curr_pmf)
						{
							$new_pmf_esc=escapeshellarg($new_pmf);
							exec("call_qcsapi SSID_set_pmf wifi0 $new_ssid_esc $new_pmf_esc");
						}
						//Set Protocol
						if ($new_proto != $curr_proto)
						{
							set_sta_proto($new_ssid,$new_proto);
						}
						//Set Passphrase
						if (strcmp($new_psk, $curr_psk) != 0)
						{
							$new_psk_esc = escapeshellarg(escape_any_characters($new_psk));
							if($new_is_psk == 1)
								exec("call_qcsapi SSID_set_pre_shared_key wifi0 $new_ssid_esc 0 $new_psk_esc");
							else
								exec("call_qcsapi SSID_set_key_passphrase wifi0 $new_ssid_esc 0 $new_psk_esc;");
						}
					}
				}
				//set band
				if ($new_band!=$curr_band)
				{
					$new_band_esc=escapeshellarg($new_band);
					exec("iwpriv wifi0 mode	$new_band_esc");
					write_wireless_conf("band",$new_band);
					$need_reboot=1;
				}
				//set bw
				if ($new_bw!=$curr_bw)
				{
					$new_bw_esc=escapeshellarg($new_bw);
					exec("call_qcsapi set_bw wifi0 $new_bw_esc");
					write_wireless_conf("bw",$new_bw);
				}
			}
			else if ($curr_mode == "Repeater")
			{
				$new_sta_ssid=$_POST['txt_sta_essid'];
				$new_sta_is_psk=$_POST['sta_is_psk'];
				$new_sta_psk=$_POST['txt_sta_passphrase'];
				$new_sta_proto=$_POST['cmb_sta_encryption'];
				$new_sta_pmf=$_POST['cmb_sta_pmf'];
				$new_channel=$_POST['cmb_channel'];
				$new_sec_channel=$_POST['cmb_sec_channel'];
				$new_broadcast=$_POST['chk_broadcast'];
				$new_broadcast=($new_broadcast == "1")? "TRUE" : "FALSE";
				$new_wpsstate=$_POST['cmb_wpsstate'];
				$new_priority=$_POST['cmb_priority'];
				$new_bintval=$_POST['txt_beaconinterval'];
				$new_dtim_period=$_POST['txt_dtimperiod'];
				$new_short_gi=$_POST['chb_shortgi'];
				$new_vlan=$_POST['txt_vlan'];
				$new_short_gi=($new_short_gi == "1")? "TRUE" : "FALSE";
				$radius_count = $_POST['NumRowsRadius'];
				$chkbox_arr = $_POST['chk_box_radius'];
				$chkbox_arr = explode(",", $chkbox_arr);
				$new_ieee80211r = $_POST['chk_ieee80211r'];
				$new_ieee80211r = ($new_ieee80211r == "1") ? 1 : 0;
				$new_11r_mob_id = $_POST['txt_mobility_domain'];
				$new_11r_nas_id = $_POST['txt_nas_id'];
				$new_11r_r1kh_id = $_POST['txt_r1kh_id'];
				$new_11r_ft_over_ds = $_POST['chk_ft_over_ds'];
				$new_11r_ft_over_ds = ($new_11r_ft_over_ds == "1") ? 1 : 0;
				$new_11v_btm_cap = $_POST['chk_11v_btm_cap'];
				$new_11v_btm_cap = ($new_11v_btm_cap == "1") ? 1 : 0;
				$new_11k_neigh_repo = $_POST['chk_11k_neigh_repo'];
				$new_11k_neigh_repo = ($new_11k_neigh_repo == "1") ? 1 : 0;

				$add_radius = $_POST['add_radius'];
				$del_radius = $_POST['del_radius'];

				//DELETE
				if ($del_radius == 1) {
					for($i = 0; $i <= $radius_count; $i++) {
						if ($chkbox_arr[$i] == "true")
						{
							$del_val = $_POST['txtbox_radius'][$i];
							$del_val = explode(" ", $del_val);
							$del_val_0_esc = escapeshellarg($del_val[0]);
							$del_val_1_esc = escapeshellarg($del_val[1]);
							exec("call_qcsapi del_radius_auth_server_cfg $curr_ap_interface $del_val_0_esc $del_val_1_esc");
						}
					}
				}

				//ADD
				if ($add_radius == 1) {
					for($i = 0; $i < $radius_count; $i++) {
						$add_val = $_POST['txtbox_radius'][$i];
						$add_val = explode(",", $add_val);
						$add_val0 = explode(" ",$add_val[0]);
						$add_val0_esc0 = escapeshellarg($add_val0[0]);
						$add_val0_esc1 = escapeshellarg($add_val0[1]);
						$add_val0_esc2 = escapeshellarg($add_val0[2]);
						exec("call_qcsapi add_radius_auth_server_cfg $curr_ap_interface $add_val0_esc0 $add_val0_esc1 $add_val0_esc2");
					}
				}

				//Set SSID;
				if ($new_ssid != $curr_ssid)
				{
					if ($curr_soniq_role == "master")
					{
						$new_ssid_esc = escape_char_for_qwebapi($new_ssid);
						exec("qweb set_inactive Device.WiFi.SSID.{0}.SSID \"\\\"$new_ssid_esc\\\"\"");
					}
					else
					{
						$new_ssid_esc = escape_any_characters($new_ssid);
						exec("call_qcsapi set_ssid $curr_ap_interface \"$new_ssid_esc\"");
					}
				}
				//set band
				if ($new_band!=$curr_band)
				{
					$new_band_esc=escapeshellarg($new_band);
					exec("iwpriv $curr_ap_interface mode $new_band_esc");
					write_wireless_conf("band",$new_band);
					$need_reboot=1;
				}
				//set bw
				if ($new_bw!=$curr_bw)
				{
					$new_bw_esc=escapeshellarg($new_bw);
					exec("call_qcsapi set_bw wifi0 $new_bw_esc");
					write_wireless_conf("bw",$new_bw);
				}
				//Set Channel
				if ($new_channel != $curr_channel)
				{
					$new_channel_esc=escapeshellarg($new_channel);
					if ($curr_region == "none")
					{
						exec("call_qcsapi set_channel wifi0 $new_channel_esc");
					}
					else
					{
						exec("call_qcsapi set_regulatory_channel wifi0 $new_channel_esc $curr_region 0");
					}

					write_wireless_conf("channel",$new_channel);
					sleep(1);
				}
				//Set Secondary channel
				if ($new_sec_channel!=$curr_sec_channel)
				{
					if ($new_sec_channel > $curr_channel)
						exec("call_qcsapi set_sec_chan wifi0 $curr_channel 0");
					else
						exec("call_qcsapi set_sec_chan wifi0 $curr_channel 1");
				}
				//Set PMF
				if ($new_pmf!=$curr_pmf)
				{
					$new_pmf_esc=escapeshellarg($new_pmf);
					exec("call_qcsapi update_persistent_param $curr_ap_interface pmf $new_pmf_esc");
					exec("call_qcsapi set_pmf $curr_ap_interface $new_pmf_esc");
				}
				//Set Protocol
				if ($new_proto != $curr_proto)
				{
					if ($curr_soniq_role == "master")
					{
						if ($new_proto == "NONE")
						{
							exec("qweb set_inactive Device.WiFi.AccessPoint.{0}.Security.ModeEnabled \\\"None\\\"");
						}
						else if ($new_proto == "11i")
						{
							exec("qweb set_inactive Device.WiFi.AccessPoint.{0}.Security.ModeEnabled \\\"WPA2-Personal\\\"");
						}
					}
					else
					{
						set_ap_proto($new_proto);
					}
				}
				// DPP configurator behavior
				if($new_proto == "DPP") {
					$new_dpp_role = $_POST['cmb_dpp_role'];
					$new_dpp_enrollee = $_POST['cmb_dpp_enrollee'];
					$new_dpp_qrcode = $_POST['txt_dpp_qrcode'];
					if($new_dpp_role == "enrollee") {
						exec("call_qcsapi set_channel wifi0 36");
					}
					else {
						if($new_dpp_qrcode != "") {
							$curr_chan = exec("call_qcsapi get_channel wifi0");
							$new_ssid_esc = escape_any_characters($new_ssid);
							if($curr_chan != 36) {
								exec("call_qcsapi disassociate wifi0");
								exec("call_qcsapi set_channel wifi0 36");
							}

							$dpp_configurator_peer_id = exec("call_qcsapi dpp_param wifi1 dpp_configurator_add curve P-256");
							$dpp_enrollee_peer_id = exec("call_qcsapi dpp_param wifi1 dpp_qr_code uri '$new_dpp_qrcode'");sleep(1);
							if($new_dpp_enrollee == "sta") {
								exec("call_qcsapi dpp_param wifi1 dpp_auth_init peer $dpp_enrollee_peer_id role configurator conf sta-dpp SSID $new_ssid_esc configurator $dpp_configurator_peer_id");
							}
							else {
								exec("call_qcsapi dpp_param wifi1 dpp_auth_init peer $dpp_enrollee_peer_id role configurator conf ap-dpp SSID $new_ssid_esc configurator $dpp_configurator_peer_id");
							}
						}
					}
				}
				//Set Passphrase
				if (strcmp($new_psk, $curr_psk) != 0 && $new_proto != "DPP")
				{
					if ($curr_soniq_role == "master")
					{
						$new_psk_esc = escape_char_for_qwebapi($new_psk);
						if($new_is_psk == 1)
							exec("qweb set_inactive Device.WiFi.AccessPoint.{0}.Security.PreSharedKey \"\\\"$new_psk_esc\\\"\"");
						else
							exec("qweb set_inactive Device.WiFi.AccessPoint.{0}.Security.KeyPassphrase \"\\\"$new_psk_esc\\\"\"");
					}
					else
					{
						$new_psk_esc = escape_any_characters($new_psk);
						if($new_is_psk == 1)
							exec("call_qcsapi set_pre_shared_key $curr_ap_interface 0 \"$new_psk_esc\"");
						else
							exec("call_qcsapi set_passphrase $curr_ap_interface 0 \"$new_psk_esc\"");
					}
				}
				//set group key interval
				if ($new_proto == "11i" || $new_proto == "WPAand11i" || $new_proto == "WPA2-EAP" || $new_proto == "11i_pmf"
					|| $new_proto == "SAE" || $new_proto == "SAE-WPA-PSK")
				{
					if ($new_group_key_interval != $curr_group_key_interval)
					{
						$new_group_key_interval_esc = escape_any_characters($new_group_key_interval);
						exec("call_qcsapi set_group_key_interval $curr_ap_interface \"$new_group_key_interval_esc\"");
					}
				}
				if ($new_proto == "WPA2-EAP" || $new_proto == "WPAand11i-EAP")
				{
					//Set EAP radius ipaddr
					if ($new_radius_ipaddr != $curr_radius_ipaddr)
					{
						$new_radius_ipaddr_esc = escape_any_characters($new_radius_ipaddr);
						exec("call_qcsapi set_eap_radius_ipaddr $curr_ap_interface \"$new_radius_ipaddr_esc\"");
					}
					//Set EAP radius port
					if (($new_radius_port != $curr_radius_port) || ($new_radius_port == "1812"))
					{
						$new_radius_port_esc = escape_any_characters($new_radius_port);
						exec("call_qcsapi set_eap_radius_port $curr_ap_interface \"$new_radius_port_esc\"");
					}
					//Set EAP shared secret
					if ($new_shared_key != $curr_shared_key)
					{
						$new_shared_key_esc = escape_any_characters($new_shared_key);
						exec("call_qcsapi set_eap_shared_key $curr_ap_interface \"$new_shared_key_esc\"");
					}
				}
				//Set Broadcast SSID
				if ($new_broadcast != $curr_broadcast){
					$new_broadcast_esc=escapeshellarg($new_broadcast);
					exec("call_qcsapi set_option $curr_ap_interface broadcast_SSID $new_broadcast_esc");
				}
				//Set Priority
				if($curr_priority != $new_priority)
				{
					$new_priority_esc=escapeshellarg($new_priority);
					exec("call_qcsapi set_priority $curr_ap_interface $new_priority_esc");
					$modify_flag=1;
				}
				//Set Beacon Interval
				if ($new_bintval != $curr_bintval)
				{
					$new_bintval_esc=escapeshellarg($new_bintval);
					exec("call_qcsapi set_beacon_interval $curr_ap_interface $new_bintval_esc");
				}
				//Set DTIM Period
				if ($new_dtim_period != $curr_dtim_period)
				{
					$new_dtim_period_esc=escapeshellarg($new_dtim_period);
					exec("call_qcsapi set_dtim $curr_ap_interface $new_dtim_period_esc");
				}
				//Set Short GI
				if ($new_short_gi != $curr_short_gi)
				{
					$new_short_gi_esc=escapeshellarg($new_short_gi);
					exec("call_qcsapi set_option wifi0 short_gi $new_short_gi_esc");
				}
				//Set VLAN
				if ($curr_vlan!=$new_vlan)
				{
					$modify_flag=1;
					$new_vlan_esc=escapeshellarg($new_vlan);

					//Remove vlan tag for the device
					if ($new_vlan=="")
					{
						exec("call_qcsapi vlan_config $curr_ap_interface unbind $curr_vlan");
						exec("call_qcsapi vlan_config $curr_ap_interface disable");
					}
					//Change a vlan tag for the device
					else
					{
						if($curr_vlan=="")
						{
							exec("call_qcsapi vlan_config $curr_ap_interface enable");
							exec("call_qcsapi vlan_config $curr_ap_interface bind $new_vlan_esc");
						}
						else
						{
							exec("call_qcsapi vlan_config $curr_ap_interface unbind $curr_vlan");
							exec("call_qcsapi vlan_config $curr_ap_interface bind $new_vlan_esc");
						}
					}
				}
				//Change ieee80211r setting
				if ($new_ieee80211r != $curr_ieee80211r)
				{
					$new_ieee80211r_esc = escapeshellarg($new_ieee80211r);
					exec("call_qcsapi set_ieee80211r $curr_ap_interface $new_ieee80211r_esc");
				}

				if($new_ieee80211r == "1")
				{
					if ($new_11r_mob_id != $curr_11r_mob_id)
					{
						$new_11r_mob_id_esc = escapeshellarg($new_11r_mob_id);
						exec("call_qcsapi set_11r_mobility_domain $curr_ap_interface $new_11r_mob_id_esc");
					}
					if ($new_11r_nas_id != $curr_11r_nas_id)
					{
						$new_11r_nas_id_esc = escapeshellarg($new_11r_nas_id);
						exec("call_qcsapi set_11r_nas_id $curr_ap_interface $new_11r_nas_id_esc");
					}
					if ($new_11r_r1kh_id != $curr_11r_r1kh_id)
					{
						$new_11r_r1kh_id_esc = escapeshellarg($new_11r_r1kh_id);
						if ($new_11r_r1kh_id_esc == "")
							$new_11r_r1kh_id_esc = 0;
						exec("call_qcsapi set_11r_r1_key_holder $curr_ap_interface $new_11r_r1kh_id_esc");
					}
					$new_11r_ft_over_ds_esc = escapeshellarg($new_11r_ft_over_ds);
					exec("call_qcsapi set_11r_ft_over_ds $curr_ap_interface $new_11r_ft_over_ds_esc");
				}
				/* Change ieee80211v setting */
				if ($new_11v_btm_cap != $curr_11v_btm_cap)
				{
					write_wireless_conf("btm_cap",$new_11v_btm_cap);
					$new_11v_btm_cap_esc = escapeshellarg($new_11v_btm_cap);
					exec("call_qcsapi set_btm_cap $curr_ap_interface $new_11v_btm_cap_esc");
				}
				/* Change ieee80211k setting */
				if ($new_11k_neigh_repo != $curr_11k_neigh_repo)
				{
					write_wireless_conf("neigh_repo",$new_11k_neigh_repo);
					$new_11k_neigh_repo_esc = escapeshellarg($new_11k_neigh_repo);
					exec("call_qcsapi set_rm_neigh_report $curr_ap_interface $new_11k_neigh_repo_esc");
				}

				if ($new_sta_proto == "DPP") {
					set_sta_proto($new_sta_ssid,$new_sta_proto);
				}
				else {
					//Set STA SSID
					if ($new_sta_ssid != $curr_sta_ssid && $new_sta_ssid != "")
					{
						$new_sta_ssid_esc = escapeshellarg(escape_any_characters($new_sta_ssid));
						$tmp=exec("call_qcsapi verify_ssid wifi0 $new_sta_ssid_esc");
						if (is_qcsapi_error($tmp))
						{
							exec("call_qcsapi create_ssid wifi0 $new_sta_ssid_esc");
						}
						//Set PMF
						$new_sta_pmf_esc=escapeshellarg($new_sta_pmf);
						exec("call_qcsapi SSID_set_pmf wifi0 $new_sta_ssid_esc $new_sta_pmf_esc");

						//Set Protocol
						set_sta_proto($new_sta_ssid,$new_sta_proto);

						//Set Passphrase
						$new_sta_psk_esc = escapeshellarg(escape_any_characters($new_sta_psk));
						if($new_sta_is_psk == 1)
							exec("call_qcsapi SSID_set_pre_shared_key wifi0 $new_sta_ssid_esc 0 $new_sta_psk_esc");
						else
							exec("call_qcsapi SSID_set_key_passphrase wifi0 $new_sta_ssid_esc 0 $new_sta_psk_esc;");
						exec("call_qcsapi associate wifi0 $new_sta_ssid_esc");
					}
					else if($new_sta_ssid != "")
					{
						$new_sta_ssid_esc = escapeshellarg(escape_any_characters($new_sta_ssid));
						//Set PMF
						if ($new_sta_pmf!=$curr_sta_pmf)
						{
							$new_sta_pmf_esc=escapeshellarg($new_sta_pmf);
							exec("call_qcsapi SSID_set_pmf wifi0 $new_sta_ssid_esc $new_sta_pmf_esc");
						}
						//Set Protocol
						if ($new_sta_proto != $curr_sta_proto)
						{
							set_sta_proto($new_sta_ssid,$new_sta_proto);
						}
						//Set Passphrase
						if (strcmp($new_sta_psk, $curr_sta_psk) != 0)
						{
							$new_sta_psk_esc = escapeshellarg(escape_any_characters($new_sta_psk));
							if($new_sta_is_psk == 1)
								exec("call_qcsapi SSID_set_pre_shared_key wifi0 $new_sta_ssid_esc 0 $new_sta_psk_esc");
							else
								exec("call_qcsapi SSID_set_key_passphrase wifi0 $new_sta_ssid_esc 0 $new_sta_psk_esc;");
						}
						exec("call_qcsapi associate wifi0 $new_sta_ssid_esc");
					}
				}

				if($modify_flag == 1)
				{
					$old_line=exec("cat $per_ssid_conf | grep $curr_ap_interface");
					$new_line="$curr_ap_interface:priority=$new_priority";
					if ($new_vlan != "")
						$new_line="$curr_ap_interface:priority=$new_priority&vlan=$new_vlan";

					$per_ssid_content=file_get_contents($per_ssid_conf);
					$per_ssid_content=str_replace($old_line,$new_line,$per_ssid_content);
					$tmp=file_put_contents($per_ssid_conf, $per_ssid_content);
				}
			}

			if ($curr_mode == "Station") {
				$set_ssid = $curr_ssid;
				if ($new_ssid != $curr_ssid)
					$set_ssid = $new_ssid;
			}

			if ($curr_mode == "Repeater") {
				$set_ssid = $curr_sta_ssid;
				if ($new_sta_ssid != $curr_sta_ssid)
					$set_ssid = $new_sta_ssid;
			}

			set_sae_owe_groups($new_mode, $new_proto, $new_sta_proto, $new_ssid, $new_sta_ssid);
		}
		//Set MCS Index
		if (strcmp($new_mcs,$curr_mcs)!=0 || strcmp($new_nss,$curr_nss)!=0)
		{
			if (($new_band == "11ac" && $new_nss == "0") ||
				($new_mcs == "0" && $new_band != "11ac"))
			{
				exec("call_qcsapi set_option wifi0 autorate TRUE");
			}
			else
			{
				if ($new_band == "11ac")
				{
					// Concate nss and mcs eg: "mcs402"
					$new_mcs = $new_nss.$new_mcs;
				}
				$new_mcs_esc=escapeshellarg($new_mcs);
				exec("call_qcsapi set_mcs_rate wifi0 $new_mcs_esc");
			}
		}
		if ($new_soniq == "")
			$new_soniq = "0";
		else if ($new_soniq == "on")
			$new_soniq = "1";

		if ($new_soniq != $curr_soniq)
		{
			write_wireless_conf("qsteer",$new_soniq);
			if ($new_soniq == "1")
			{
				exec("start-stop-daemon -S -b -x /etc/init.d/S90qsteer -- start");
			}
			else if ($new_soniq == "0")
			{
				exec("/etc/init.d/S90qsteer stop");
			}
		}
		if ($curr_soniq_role == "master")
		{
			exec("qwebcfg get_inactive /tmp/new_ap_cfg");
			exec("qcomm_cli update_cfg /tmp/new_ap_cfg");
			exec("qweb clean_inactive");
			sleep(1);
		}
		if ($need_reboot == 1)
		{
			$_SESSION['qtn_can_reboot']=TRUE;
			header('Location: system_rebooted.php');
			exit();
		}
	}
}

get_value();
if (isset($_POST['action']))
{
	if (!(isset($_POST['csrf_token']) && $_POST['csrf_token'] === get_session_token())) {
		header('Location: login.php');
		exit();
	}
	set_value();
	get_value();
}
?>
<script type="text/javascript">
var mode = "<?php echo $curr_mode; ?>";
var curr_ssid = decodeURIComponent("<?php echo rawurlencode($curr_ssid); ?>");
var curr_sta_ssid = decodeURIComponent("<?php echo rawurlencode($curr_sta_ssid); ?>");
var curr_bw = "<?php echo $curr_bw; ?>";
var curr_pmf = "<?php echo $curr_pmf; ?>";
var curr_sta_pmf = "<?php echo $curr_sta_pmf; ?>";
var curr_proto = "<?php echo $curr_proto; ?>";
var curr_sta_proto = "<?php echo $curr_sta_proto; ?>";
var curr_sta_saegroup = "<?php echo $curr_sta_saegroups; ?>";
var curr_sta_owegroup = "<?php echo $curr_sta_owegroups; ?>";
var curr_region = "<?php echo $curr_region; ?>";
var curr_dpp_qrcode = "<?php echo $curr_dpp_qrcode; ?>";
var curr_sta_dpp_qrcode = "<?php echo $curr_sta_dpp_qrcode; ?>";

var curr_band = "<?php echo $curr_band; ?>";
var ch_list_none="<?php echo $curr_channel_list_none;?>";
var ch_list_20="<?php echo $curr_channel_list_20;?>";
var ch_list_40="<?php echo $curr_channel_list_40;?>";
var ch_list_80="<?php echo $curr_channel_list_80;?>";
var curr_soniq_role="<?php echo $curr_soniq_role;?>";
nonhex = /[^A-Fa-f0-9]/g;
nonascii = /[^\x20-\x7E]/;

var radius_arr = <?php echo '["' . implode('", "', $curr_radius) . '"]' ?>;

function isNatNumber(num)
{
	if((num != "") && (/^\d+$/.test(num)))
	{
		return true;
	}
	return false;
}
function isHex(entry)
{
	validChar='0123456789ABCDEF';	// legal chars
	strlen=entry.length;			// test string length
	if(strlen != "12")
	{
		alert("MAC Address needs to be 12 characters long");
		return false;
	}
	entry=entry.toUpperCase(); // case insensitive
	// Now scan for illegal characters
	for(idx=0;idx<strlen;idx++)
	{
		if(validChar.indexOf(entry.charAt(idx)) < 0)
		{
			alert("All characters must be hex characters (0-9 and a-f or A-F)!");
			return false;
		}
	} // end scanning
	return true;
}

function populate_encryptionlist(pmf_value)
{
	var cmb_encryption = document.getElementById("cmb_encryption");
	if (mode == "Station")
	{
		if (pmf_value != "0")
		{
			cmb_encryption.options.length = 5;
			cmb_encryption.options[0].text = "NONE-OPEN"; cmb_encryption.options[0].value = "NONE";
			cmb_encryption.options[1].text = "WPA2-AES-SHA256 Mixed"; cmb_encryption.options[1].value = "11i_pmf";
			cmb_encryption.options[2].text = "SAE"; cmb_encryption.options[2].value = "SAE";
			cmb_encryption.options[3].text = "SAE + WPA2 (mixed mode)"; cmb_encryption.options[3].value = "SAE-WPA-PSK";
			cmb_encryption.options[4].text = "OWE"; cmb_encryption.options[4].value = "OWE";
			if(pmf_value == "2") {
				cmb_encryption.options.length = 6;
				cmb_encryption.options[5].text = "DPP";
				cmb_encryption.options[5].value = "DPP";
			}
		}
		else
		{
			cmb_encryption.options.length = 2;
			cmb_encryption.options[0].text = "NONE-OPEN"; cmb_encryption.options[0].value = "NONE";
			cmb_encryption.options[1].text = "WPA2-AES"; cmb_encryption.options[1].value = "11i";
			if (curr_region != "us")
			{
				cmb_encryption.options.length = 3;
				cmb_encryption.options[2].text = "WPA2 + WPA (mixed mode)"; cmb_encryption.options[2].value = "WPAand11i";
			}
		}
	}
	else if (mode == "Access point" || mode == "Repeater")
	{
		if (pmf_value == "0") //Disabled
		{
			cmb_encryption.options.length = 3;
			cmb_encryption.options[0].text = "NONE-OPEN"; cmb_encryption.options[0].value = "NONE";
			cmb_encryption.options[1].text = "WPA2-AES"; cmb_encryption.options[1].value = "11i";
			if (curr_region != "us")
			{
				cmb_encryption.options.length = 4;
				cmb_encryption.options[2].text = "WPA2 + WPA (mixed mode)"; cmb_encryption.options[2].value = "WPAand11i";
				cmb_encryption.options[3].text = "WPA2-AES-Enterprise"; cmb_encryption.options[3].value = "WPA2-EAP";
			}
			else
			{
				cmb_encryption.options[2].text = "WPA2-AES-Enterprise"; cmb_encryption.options[2].value = "WPA2-EAP";
			}
		}
		else if (pmf_value == "1") //Enabled
		{
			cmb_encryption.options.length = 6;
			cmb_encryption.options[0].text = "NONE-OPEN"; cmb_encryption.options[0].value = "NONE";
			cmb_encryption.options[1].text = "WPA2-AES"; cmb_encryption.options[1].value = "11i";
			cmb_encryption.options[2].text = "WPA2-AES-Enterprise"; cmb_encryption.options[2].value = "WPA2-EAP";
			cmb_encryption.options[3].text = "SAE"; cmb_encryption.options[3].value = "SAE";
			cmb_encryption.options[4].text = "SAE + WPA2 (mixed mode)"; cmb_encryption.options[4].value = "SAE-WPA-PSK";
			cmb_encryption.options[5].text = "OWE"; cmb_encryption.options[5].value = "OWE";
		}
		else if (pmf_value == "2") //Required
		{
			cmb_encryption.options.length = 7;
			cmb_encryption.options[0].text = "NONE-OPEN"; cmb_encryption.options[0].value = "NONE";
			cmb_encryption.options[1].text = "WPA2-AES-SHA256"; cmb_encryption.options[1].value = "11i_pmf";
			cmb_encryption.options[2].text = "WPA2-AES-Enterprise"; cmb_encryption.options[2].value = "WPA2-EAP";
			cmb_encryption.options[3].text = "SAE"; cmb_encryption.options[3].value = "SAE";
			cmb_encryption.options[4].text = "SAE + WPA2 (mixed mode)"; cmb_encryption.options[4].value = "SAE-WPA-PSK";
			cmb_encryption.options[5].text = "OWE"; cmb_encryption.options[5].value = "OWE";
			cmb_encryption.options[6].text = "DPP"; cmb_encryption.options[6].value = "DPP";
		}
	}
	var encryption_in_list = false;
	for(var i = 0; i < cmb_encryption.options.length; i++) {
		var option = cmb_encryption.options[i];
		if(option.value == curr_proto) {
			encryption_in_list = true;
			break;
		}
	}
	if(encryption_in_list) {
		set_control_value("cmb_encryption", curr_proto, "combox");
	}
	else {
		set_control_value("cmb_encryption", "NONE", "combox");
	}
	cmb_encryption.addEventListener("change", modechange(cmb_encryption));
}

function populate_sta_encryptionlist(sta_pmf_value)
{
	var cmb_sta_encryption = document.getElementById("cmb_sta_encryption");
	if (mode == "Repeater")
	{
		if (sta_pmf_value != "0")
		{
			cmb_sta_encryption.options.length = 5;
			cmb_sta_encryption.options[0].text = "NONE-OPEN"; cmb_sta_encryption.options[0].value = "NONE";
			cmb_sta_encryption.options[1].text = "WPA2-AES-SHA256 Mixed"; cmb_sta_encryption.options[1].value = "11i_pmf";
			cmb_sta_encryption.options[2].text = "SAE"; cmb_sta_encryption.options[2].value = "SAE";
			cmb_sta_encryption.options[3].text = "SAE + WPA2 (mixed mode)"; cmb_sta_encryption.options[3].value = "SAE-WPA-PSK";
			cmb_sta_encryption.options[4].text = "OWE"; cmb_sta_encryption.options[4].value = "OWE";
			if(sta_pmf_value == "2") {
				cmb_sta_encryption.options.length = 6;
				cmb_sta_encryption.options[5].text = "DPP";
				cmb_sta_encryption.options[5].value = "DPP";
			}
		}
		else
		{
			cmb_sta_encryption.options.length = 2;
			cmb_sta_encryption.options[0].text = "NONE-OPEN"; cmb_sta_encryption.options[0].value = "NONE";
			cmb_sta_encryption.options[1].text = "WPA2-AES"; cmb_sta_encryption.options[1].value = "11i";
			if (curr_region != "us")
			{
				cmb_sta_encryption.options.length = 3;
				cmb_sta_encryption.options[2].text = "WPA2 + WPA (mixed mode)"; cmb_sta_encryption.options[2].value = "WPAand11i";
			}
		}
	}
	var encryption_in_list = false;
	for(var i = 0; i < cmb_sta_encryption.options.length; i++) {
		var option = cmb_sta_encryption.options[i];
		if(option.value == curr_sta_proto) {
			encryption_in_list = true;
			break;
		}
	}
	if(encryption_in_list) {
		set_control_value("cmb_sta_encryption", curr_sta_proto, "combox");
	}
	else {
		set_control_value("cmb_sta_encryption", "NONE", "combox");
	}
	cmb_sta_encryption.addEventListener("change", modechange(cmb_sta_encryption));
}

function populate_channellist(list_index, band)
{
	var cmb_channel = document.getElementById("cmb_channel");

	switch (list_index)
	{
	case 0:
		var ch_list=ch_list_none;
		break;
	case 20:
		var ch_list=ch_list_20;
		break;
	case 40:
		var ch_list=ch_list_40;
		break;
	case 80:
		var ch_list=ch_list_80;
		break;
	}

	var curr_channel="<?php echo $curr_channel; ?>";
	var tmp=ch_list.split(",");
	var n=0;

	if (mode == "Access point" || mode == "Repeater")
	{
		cmb_channel.options.add(new Option("Auto","0"));
		n=1;
		if( curr_channel=="0")
			cmb_channel.options[0].selected = true;
	}
	for(var i=0;i<tmp.length;i++)
	{
		if (band == "11ng" || band == "11b" || band == "11g")
		{
			if (Number(tmp[i]) >= "1" && Number(tmp[i]) <= "13")
				cmb_channel.options.add(new Option(tmp[i],tmp[i]));
		}
		else
		{
			if (Number(tmp[i]) >= "36" && Number(tmp[i]) <= "165")
				cmb_channel.options.add(new Option(tmp[i],tmp[i]));
		}
	}
	for (var i = 0; i < cmb_channel.options.length; i++)
	{
		if (cmb_channel.options[i].text == curr_channel)
			cmb_channel.options[i].selected = true;
	}
}

function clear_channellist()
{
	var cmb_channel = document.getElementById("cmb_channel");
	for (var i=cmb_channel.length;i>0 ;i-- )
	{
		cmb_channel.remove(i-1);
	}
}

function populate_sec_channel_list()
{
	var cmb_sec_channel = document.getElementById("cmb_sec_channel");
	var curr_channel = parseInt("<?php echo $curr_channel; ?>");
	var curr_sec_channel = parseInt("<?php echo $curr_sec_channel; ?>");
	var max_dual_channel = 9;
	var curr_sec_channel_tmp;

	if (curr_region == "us")
	{
		max_dual_channel = 7;
	}

	if (curr_channel == 0)
	{
		set_disabled('cmb_sec_channel', true);
	}
	else if (curr_channel < 5)
	{
		cmb_sec_channel.options.length = 1;
		cmb_sec_channel.options[0].text = curr_sec_channel.toString();
		cmb_sec_channel.options[0].value = curr_sec_channel.toString();
	}
	else if (curr_channel >= 5 && curr_channel <= max_dual_channel)
	{
		cmb_sec_channel.options.length = 2;

		curr_sec_channel_tmp = curr_channel - 4;
		cmb_sec_channel.options[0].text = curr_sec_channel_tmp.toString();
		cmb_sec_channel.options[0].value = curr_sec_channel_tmp.toString();

		curr_sec_channel_tmp = curr_channel + 4;
		cmb_sec_channel.options[1].text = curr_sec_channel_tmp.toString();
		cmb_sec_channel.options[1].value = curr_sec_channel_tmp.toString();

		if (curr_sec_channel < curr_channel)
			cmb_sec_channel.options[0].selected = true;
		else
			cmb_sec_channel.options[1].selected = true;
	}
	else
	{
		cmb_sec_channel.options.length = 1;
		cmb_sec_channel.options[0].text = curr_sec_channel.toString();
		cmb_sec_channel.options[0].value = curr_sec_channel.toString();
	}
}

function populate_bwlist()
{
	var cmb_bw = document.getElementById("cmb_bandwidth");

	if (curr_band == "11ac" || curr_band == "11acOnly")
	{
		cmb_bw.options.length = 3;
		cmb_bw.options[0].text = "20MHz"; cmb_bw.options[0].value = "20";
		cmb_bw.options[1].text = "40MHz"; cmb_bw.options[1].value = "40";
		cmb_bw.options[2].text = "80MHz"; cmb_bw.options[2].value = "80";
	}
	else if (curr_band == "11na" || curr_band == "11ng" || curr_band == "11nOnly")
	{
		cmb_bw.options.length = 2;
		cmb_bw.options[0].text = "20MHz"; cmb_bw.options[0].value = "20";
		cmb_bw.options[1].text = "40MHz"; cmb_bw.options[1].value = "40";
	}
	else
	{
		cmb_bw.options.length = 1;
		cmb_bw.options[0].text = "20MHz"; cmb_bw.options[0].value = "20";
	}

	if (curr_bw == "20")
	{
		cmb_bw.options[0].selected = true;
	}
	else if (curr_bw == "40")
	{
		cmb_bw.options[1].selected = true;
	}
	else if (curr_bw == "80")
	{
		cmb_bw.options[2].selected = true;
	}
}

function populate_bandlist()
{
	var cmb_band = document.getElementById("cmb_wirelessmode");
	var freq_band = document.getElementById("cmb_frequencyband");

	var rf_chipid = "<?php echo $rf_chipid; ?>";
	var vht_status = "<?php echo $vht_status; ?>";

	if (rf_chipid == "2")
	{
		if (mode=="Access point" || mode=="Repeater")
		{
			freq_band.options.add(new Option("2.4GHZ","2.4GHZ"));
			freq_band.options.add(new Option("5GHZ","5GHZ"));
			if (curr_band == "11a" || curr_band == "11na"
				|| curr_band == "11ac" || curr_band == "11acOnly"
				|| curr_band == "11nOnly")
			{
				freq_band.options[1].selected = true;
				cmb_band.options.add(new Option("802.11a","11a"));
				cmb_band.options.add(new Option("802.11an","11na"));
				cmb_band.options.add(new Option("802.11nOnly","11nOnly"));
				if (vht_status == "1")
				{
					cmb_band.options.add(new Option("802.11ac","11ac"));
					cmb_band.options.add(new Option("802.11acOnly","11acOnly"));
				}
			}
			else
			{
				freq_band.options[0].selected = true;
				cmb_band.options.add(new Option("802.11b","11b"));
				cmb_band.options.add(new Option("802.11bg","11g"));
				cmb_band.options.add(new Option("802.11gn","11ng"));
			}
		}
		else
		{
			cmb_band.options.add(new Option("802.11a","11a"));
			cmb_band.options.add(new Option("802.11b","11b"));
			cmb_band.options.add(new Option("802.11bg","11g"));
			cmb_band.options.add(new Option("802.11an","11na"));
			cmb_band.options.add(new Option("802.11gn","11ng"));
			cmb_band.options.add(new Option("802.11nOnly","11nOnly"));
			if (vht_status == "1")
			{
				cmb_band.options.add(new Option("802.11ac","11ac"));
				cmb_band.options.add(new Option("802.11acOnly","11acOnly"));
			}
		}
	}
	else if (rf_chipid == "0")
	{
		cmb_band.options.add(new Option("802.11gn","11ng"));
		cmb_band.options.add(new Option("802.11b","11b"));
		cmb_band.options.add(new Option("802.11bg","11g"));

		freq_band.options.add(new Option("2.4GHZ","2.4GHZ"));
		set_disabled('cmb_frequencyband', true);
	}
	else
	{
		cmb_band.options.add(new Option("802.11a","11a"));
		cmb_band.options.add(new Option("802.11an","11na"));
		cmb_band.options.add(new Option("802.11nOnly","11nOnly"));
		if (vht_status == "1")
		{
			cmb_band.options.add(new Option("802.11ac","11ac"));
			cmb_band.options.add(new Option("802.11acOnly","11acOnly"));
		}

		freq_band.options.add(new Option("5GHZ","5GHZ"));
		set_disabled('cmb_frequencyband', true);
	}

	for (var i = 0; i < cmb_band.options.length; i++)
	{
		if (cmb_band.options[i].value == curr_band)
			cmb_band.options[i].selected = true;
	}
}

function populate_11ac_mcslist(bw, nss)
{
	nsslist	= document.getElementById("cmb_nss");
	if (!nsslist) return;

	mcslist = document.getElementById("cmb_txrate");
	if (!mcslist) return;

		nsslist.options.length = 5;
	nsslist.options[0].text = "Auto"; nsslist.options[0].value = 0;

	for (i=1; i< nsslist.options.length; i++)
	{
		nsslist.options[i].text = i;
		nsslist.options[i].value = "mcs"+i;
	}

	mcs_index = "<?php echo $curr_mcs; ?>" ;

	if (nss == "0")
	{
		mcslist.options.length = 1;
		mcslist.options[0].text = "Auto"; mcslist.options[0].value = "0";
	}
	else
	{
		if (bw == "20")
		{
			mcslist.options.length = 9;
			if (nss == "3")
			{
				mcslist.options.length = 10;
			}
			for (i=0; i < mcslist.options.length; i++)
			{
				mcslist.options[i].text = "MCS"+i;
				mcslist.options[i].value = "0"+i;
			}
		}
		if (bw == "40")
		{
			mcslist.options.length = 10;
			for (i=0; i < mcslist.options.length; i++)
			{
				mcslist.options[i].text = "MCS"+i;
					mcslist.options[i].value = "0"+i;
			}
		}
		if (bw == "80")
		{
			mcslist.options.length = 10;
			if (nss == "3")
			{
				mcslist.options.length = 9;
				if (mcs_index > 6)
					mcs_index = mcs_index - 1;
			}
			for (i=0,j=0; j < mcslist.options.length; i++)
			{
				if (nss == "3" && i == 6)
				{
					continue;
				}
				mcslist.options[j].text = "MCS"+i;
					mcslist.options[j].value = "0"+i;
				j++;
			}
		}
	}
	nsslist.options[nss].selected = true;
	mcslist.options[mcs_index].selected = true;
}

function populate_mcslist()
{
	mcslist = document.getElementById("cmb_txrate");
	if(!mcslist) return;

	set_visible('tr_nss', false);

	mcslist.options.length = 77;

	mcslist.options[0].text = "Auto"; mcslist.options[0].value = "0";
	for(i=0;i<=31;i++)
	{
		{mcslist.options[i+1].text = "MCS"+i; mcslist.options[i+1].value = "mcs"+i;}
	}

	for(i=33;i<=76;i++)
	{
		{mcslist.options[i].text = "MCS"+i; mcslist.options[i].value = "mcs"+i;}
	}

	mcs_value = "<?php echo $curr_mcs; ?>" ;
	mcs_index = 0;
	if(mcs_value.length >= 4&&mcs_value.length<=6)
	{
		mcs_index = mcs_value.substr(3);
		var tmp = "+";
		if(31>=mcs_index)
		mcs_index = eval(mcs_index+tmp+1);
	}
	mcslist.options[mcs_index].selected = true;
}

function update_11r_config_display(v)
{
	set_visible('tr_ln',v);
	/* Mobility Domain */
	set_visible('txt_mobility_domain',v);
	set_visible('txt_mobility_domain_label',v);
	/* Nas ID */
	set_visible('txt_nas_id',v);
	set_visible('txt_nas_id_label',v);
	/* R1 key holder */
	set_visible('txt_r1kh_id',v);
	set_visible('txt_r1kh_id_label',v);
	/* FT over DS */
	set_visible('chk_ft_over_ds',v);
	set_visible('chk_ft_over_ds_label',v);
}

function modechange(obj)
{
	var v;
	var ieee80211r = document.getElementById("chk_ieee80211r");

	if(typeof modechange.toggle == 'undefined') modechange.toggle = 0;
	if(obj.name == "cmb_devicemode")
	{
		v = (obj.value == mode)? false : true;
		modechange.toggle = (modechange.toggle == 0) ? 1 : 0;
		set_disabled('cmb_pmf', v);
		set_disabled('cmb_encryption', v);
		set_disabled('cmb_frequencyband', v);
		set_disabled('cmb_wirelessmode', v);
		set_disabled('cmb_bandwidth', v);
		set_disabled('cmb_sec_channel', v);
		set_disabled('txt_passphrase', v);
		set_disabled('txt_radius_ipaddr', v);
		set_disabled('cmb_sta_saegroup', v);
		set_disabled('cmb_sta_owegroup', v);
		set_disabled('txt_radius_port', v);
		set_disabled('txt_shared_key', v);
		set_disabled('txt_group_key_interval', v);
		set_disabled('cmb_channel', v);
		set_disabled('txt_essid', v);
		set_disabled('btn_aplist', v);
		set_disabled('chk_broadcast', v);
		//set_disabled('cmb_txrate', v);
		set_disabled('cmb_priority', v);
		set_disabled('cmb_nss', v);
		set_disabled('txt_beaconinterval', v);
		set_disabled('txt_dtimperiod', v);
		set_disabled('chb_shortgi', v);
		set_disabled('txt_vlan', v);
		set_disabled('chk_ieee80211r', v);
		set_disabled('txt_mobility_domain', v);
		set_disabled('txt_nas_id', v);
		set_disabled('txt_r1kh_id', v);
		set_disabled('chk_ft_over_ds', v);
		set_disabled('chk_11v_btm_cap', v);
		set_disabled('chk_11k_neigh_repo', v);
		set_visible('tr_warning_basic', v);
		set_disabled("txt_sta_essid", v);
		set_disabled("btn_sta_aplist", v);
		set_disabled("cmb_sta_pmf", v);
		set_disabled("cmb_sta_encryption", v);
		set_disabled("txt_sta_passphrase", v);
		set_disabled("cmb_dpp_enrollee", v);
		set_disabled("cmb_dpp_role", v);
		set_disabled("txt_dpp_qrcode", v);
		set_disabled("txt_sta_dpp_qrcode", v);
		if(curr_sta_ssid != "" && mode == "Repeater")
			set_disabled('cmb_channel', true);
	}

	if(obj.name == 'cmb_pmf')
	{
		var cmb_pmf = document.getElementById("cmb_pmf");
		populate_encryptionlist(cmb_pmf.value);
	}

	if(obj.name == 'cmb_sta_pmf')
	{
		var cmb_sta_pmf = document.getElementById("cmb_sta_pmf");
		populate_sta_encryptionlist(cmb_sta_pmf.value);
	}

	if(obj.name == 'cmb_encryption')
	{
		set_visible('tr_passphrase', true);
		set_visible('tr_saegroups', false);
		set_visible('tr_owegroups', false);
		set_visible('tr_pmf', true);
		set_visible('tr_essid', true);
		set_visible('radius_display_table', false);
		set_visible('radius_table', false);
		set_visible('tr_group_key_interval', false);
		set_visible('dpp_field', false);
		set_visible('dpp_enrollee_field', false);
		set_disabled('chk_ieee80211r', true);
		update_11r_config_display(false);
		/* To check the visibility of Passphrase, Radius IPAddr, Radius Port and Shared Key control */
		if (isset('cmb_pmf', '0'))
		{
			var proto = document.mainform.cmb_encryption.value;
			if (proto == "NONE" || proto == "" || proto == "WPA2-EAP" || proto =="WPAand11i-EAP" || proto == "OWE" || proto == "DPP") {
				set_visible('tr_passphrase', false);
			}
			if (proto == "WPA2-EAP" || proto == "WPAand11i-EAP") {
				set_visible('radius_display_table', true);
				set_visible('radius_table', true);
			}
			if (proto != "NONE" && mode != "Station")
			{
				set_visible('tr_group_key_interval', true);
				set_disabled('chk_ieee80211r', false);
				if(ieee80211r.checked == true)
					update_11r_config_display(true);
			}
			if(proto == "DPP" && mode == "Station") {
				set_visible('tr_passphrase', false);
				set_visible('tr_pmf', false);
				set_visible('radius_display_table', false);
				set_visible('radius_table', false);
				set_visible('dpp_field', true);
				set_visible('dpp_qrcode_field', true);
				if(document.mainform.cmb_dpp_role.value == "configurator") {
					set_visible('dpp_enrollee_field', true);
				}
				else if(curr_dpp_qrcode == "") {
					set_visible('dpp_qrcode_field', false);
				}
				if(mode == "Station") {
					set_visible('tr_essid', false);
				}
			}
		}
		else
		{
			var proto = document.mainform.cmb_encryption.value;
			if (proto == "NONE" || proto == "" || proto == "WPA2-EAP" || proto =="WPAand11i-EAP" || proto == "OWE" || proto == "DPP") {
				set_visible('tr_passphrase', false);
			}

			if (proto == "WPA2-EAP" || proto == "WPAand11i-EAP") {
				set_visible('radius_display_table', true);
				set_visible('radius_table', true);
			}
			if(mode == "Access point" || mode == "Repeater") {
				if (proto == "SAE" || proto == "SAE-WPA-PSK") {
					set_visible('tr_saegroups', true);
				}

				if (proto == "OWE") {
					set_visible('tr_owegroups', true);
				}

				if (proto != "NONE" && proto != "OWE") {
					set_visible('tr_group_key_interval', true);
					set_disabled('chk_ieee80211r', false);
					if(ieee80211r.checked == true)
						update_11r_config_display(true);
				}

				if(proto == "DPP") {
					set_visible('dpp_field', true);
					set_visible('tr_group_key_interval', false);
					set_visible('dpp_qrcode_field', true);
					if(document.mainform.cmb_dpp_role.value == "configurator") {
						set_visible('dpp_enrollee_field', true);
					}
					else if(curr_dpp_qrcode == "") {
						set_visible('dpp_qrcode_field', false);
					}
				}
			}

			if (mode == "Station") {
				set_visible('tr_sta_saegroup', false);
				set_visible('tr_sta_owegroup', false);
				if (proto == "SAE" || proto == "SAE-WPA-PSK") {
					set_visible('tr_sta_saegroup', true);
				}

				if (proto == "OWE") {
					set_visible('tr_sta_owegroup', true);
				}

				if (proto == "DPP") {
					set_visible('tr_essid', false);
					set_visible('tr_owegroups', false);
					set_visible('tr_saegroups', false);
					set_visible('tr_passphrase', false);
					set_visible('tr_pmf', false);
					set_visible('dpp_field', true);
					set_visible('dpp_qrcode_field', true);
					if(document.mainform.cmb_dpp_role.value == "configurator") {
						set_visible('dpp_enrollee_field', true);
					}
					else if(curr_dpp_qrcode == "") {
						set_visible('dpp_qrcode_field', false);
					}
				}

			}
		}
	}

	if(obj.name == 'cmb_sta_encryption')
	{
		var sta_proto = document.mainform.cmb_sta_encryption.value;
		set_visible("tr_sta_essid", true);
		set_visible('tr_sta_passphrase', true);
		set_visible('tr_sta_saegroup', false);
		set_visible('tr_sta_owegroup', false);
		set_visible('dpp_sta_qrcode_field', false);
		set_visible('tr_sta_pmf', true);
		if (isset('cmb_pmf', '0'))
		{
			if (sta_proto == "NONE" || sta_proto == "" || sta_proto == "WPA2-EAP" || sta_proto =="WPAand11i-EAP" || sta_proto == "OWE" || sta_proto == "DPP") {
				set_visible('tr_sta_passphrase', false);
			}
		}
		else
		{
			if (sta_proto == "NONE" || sta_proto == "" || sta_proto == "WPA2-EAP" || sta_proto =="WPAand11i-EAP" || sta_proto == "OWE" || sta_proto == "DPP") {
				set_visible('tr_sta_passphrase', false);
			}
			if (sta_proto == "SAE" || sta_proto == "SAE-WPA-PSK") {
				set_visible('tr_sta_saegroup', true);
			}
			if (sta_proto == "OWE") {
				set_visible('tr_sta_owegroup', true);
			}
		}

		if (sta_proto == "DPP")
		{
			set_visible("tr_sta_essid", false);
			set_visible('tr_sta_owegroup', false);
			set_visible('tr_sta_saegroup', false);
			set_visible('tr_sta_passphrase', false);
			set_visible('tr_sta_pmf', false);
			if(curr_sta_dpp_qrcode != "") {
				set_visible('dpp_sta_qrcode_field', true);
			}
		}
	}

	if(obj.name == 'cmb_frequencyband')
	{
		var cmb_band = document.getElementById("cmb_wirelessmode");
		if (isset('cmb_frequencyband', '2.4GHZ'))
		{
			cmb_band.options.length = 3;
			cmb_band.options[0].text = "802.11b"; cmb_band.options[0].value = "11b";
			cmb_band.options[1].text = "802.11bg"; cmb_band.options[1].value = "11g";
			cmb_band.options[2].text = "802.11gn"; cmb_band.options[2].value = "11ng";
			cmb_band.options[0].selected = true;
		}
		else
		{
			var vht_status = "<?php echo $vht_status; ?>";

			cmb_band.options.length = 3;
			cmb_band.options[0].text = "802.11a"; cmb_band.options[0].value = "11a";
			cmb_band.options[1].text = "802.11an"; cmb_band.options[1].value = "11na";
			cmb_band.options[2].text = "802.11nOnly";
			cmb_band.options[2].value = "11nOnly";
			if (vht_status == "1")
			{
				cmb_band.options.length += 2;
				cmb_band.options[3].text = "802.11ac"; cmb_band.options[3].value = "11ac";
				cmb_band.options[4].text = "802.11acOnly";
				cmb_band.options[4].value = "11acOnly";
			}
			cmb_band.options[0].selected = true;
		}
		modechange(cmb_wirelessmode);
	}

	if(obj.name == 'cmb_wirelessmode')
	{
		if (isset('cmb_devicemode', 'Station'))
		{
				set_visible('tr_divline',false);
		}
		set_visible('tr_nss', false);
		set_visible('tr_txrate', false);
		if (obj.value == curr_band)
		{
			set_visible('tr_warning_adv', false);
		}
		else
		{
			set_visible('tr_warning_adv', true);
		}
		var cmb_bw = document.getElementById("cmb_bandwidth");
		if (isset('cmb_wirelessmode', '11ac'))
		{
			cmb_bw.options.length = 3;
			cmb_bw.options[0].text = "20MHz"; cmb_bw.options[0].value = "20";
			cmb_bw.options[1].text = "40MHz"; cmb_bw.options[1].value = "40";
			cmb_bw.options[2].text = "80MHz"; cmb_bw.options[2].value = "80";
			cmb_bw.options[2].selected = true;
			new_band = "11ac";
			new_bw = 80;
			//Set 11ac mcslist
			set_visible('tr_divline',true);
			set_visible('tr_nss', true);
			set_visible('tr_txrate', true);
			nss = 0;
			populate_11ac_mcslist(new_bw, nss);
		}
		else if (isset('cmb_wirelessmode', '11acOnly'))
		{
			cmb_bw.options.length = 3;
			cmb_bw.options[0].text = "20MHz"; cmb_bw.options[0].value = "20";
			cmb_bw.options[1].text = "40MHz"; cmb_bw.options[1].value = "40";
			cmb_bw.options[2].text = "80MHz"; cmb_bw.options[2].value = "80";
			cmb_bw.options[2].selected = true;
			new_band = "11acOnly";
			new_bw = 80;
			//Set 11ac mcslist
			set_visible('tr_divline',true);
			set_visible('tr_nss', true);
			set_visible('tr_txrate', true);
			nss = 0;
			populate_11ac_mcslist(new_bw, nss);
		}
		else if (isset('cmb_wirelessmode', '11nOnly'))
		{
			cmb_bw.options.length = 2;
			cmb_bw.options[0].text = "20MHz"; cmb_bw.options[0].value = "20";
			cmb_bw.options[1].text = "40MHz"; cmb_bw.options[1].value = "40";
			cmb_bw.options[1].selected = true;
			new_band = "11nOnly";
			new_bw = 40;
		}
		else if (isset('cmb_wirelessmode', '11na'))
		{
			cmb_bw.options.length = 2;
			cmb_bw.options[0].text = "20MHz"; cmb_bw.options[0].value = "20";
			cmb_bw.options[1].text = "40MHz"; cmb_bw.options[1].value = "40";
			cmb_bw.options[1].selected = true;
			new_band = "11na";
			new_bw = 40;
		}
		else if (isset('cmb_wirelessmode', '11ng'))
		{
			cmb_bw.options.length = 2;
			cmb_bw.options[0].text = "20MHz"; cmb_bw.options[0].value = "20";
			cmb_bw.options[1].text = "40MHz"; cmb_bw.options[1].value = "40";
			cmb_bw.options[1].selected = true;
			new_band = "11ng";
			new_bw = 40;
		}
		else
		{
			cmb_bw.options.length = 1;
			cmb_bw.options[0].text = "20MHz"; cmb_bw.options[0].value = "20";
			cmb_bw.options[0].selected = true;
			new_band = obj.value;
			new_bw = 20;
		}
		//Set mcslist
		if (isset('cmb_wirelessmode', '11na')
			|| isset('cmb_wirelessmode', '11ng')
			|| isset('cmb_wirelessmode', '11nOnly'))
		{
			set_visible('tr_divline',true);
			set_visible('tr_txrate', true);
			populate_mcslist();
		}
		// Set respective channel list
		clear_channellist();
		if (curr_region == "none")
		{
			populate_channellist(0, new_band);
		}
		else
		{
			populate_channellist(new_bw, new_band);
		}
	}

	if (obj.name == 'cmb_bandwidth')
	{
		if (curr_region == "none")
		{
			clear_channellist();
			populate_channellist(0);
		}
		else
		{
			if (document.mainform.cmb_bandwidth.selectedIndex == 0)
			{
				clear_channellist();
				populate_channellist(20);
			}
			else if (document.mainform.cmb_bandwidth.selectedIndex == 1 )
			{
				clear_channellist();
				populate_channellist(40);
			}
			else if (document.mainform.cmb_bandwidth.selectedIndex == 2 )
			{
				clear_channellist();
				populate_channellist(80);
			}
		}
		//Set 11ac mcslist
		if (isset('cmb_wirelessmode', '11ac') || isset('cmb_wirelessmode', '11acOnly'))
		{
			nss = 0;
			populate_11ac_mcslist(obj.value, nss);
		}
	}
	//Set 11ac mcslist
	if (obj.name == 'cmb_nss')
	{
		var bw = document.getElementById("cmb_bandwidth").value;
		if(obj.value != "0")
			nss = obj.value.substr(3,1);
		else
			nss = obj.value;
		populate_11ac_mcslist(bw, nss);
	}

	if (obj.name == 'cmb_channel')
	{
		set_disabled('cmb_sec_channel', true);
	}
	if(obj.name == 'chk_ieee80211r')
	{
		if(ieee80211r.checked == false)
			update_11r_config_display(false);
		else
			update_11r_config_display(true);
	}
	if(obj.name == 'cmb_dpp_role') {
		set_visible('dpp_qrcode_field', true);
		if (document.mainform.cmb_dpp_role.selectedIndex == 0)
		{
			set_visible('dpp_qrcode_image', true);
			set_visible('dpp_enrollee_field', false);
			if(curr_dpp_qrcode == "") {
				set_visible('dpp_qrcode_field', false);
			}
			set_control_value('txt_dpp_qrcode',curr_dpp_qrcode, 'text');
		}
		else {
			set_visible('dpp_qrcode_image', false);
			set_visible('dpp_enrollee_field', true);
			set_control_value('txt_dpp_qrcode', '', 'text');
		}
	}
}

function deleteRadiusRow(tableID)
{
	var chk_box_arr = new Array();
	var table = document.getElementById(tableID);
	var rowCount = table.rows.length;
	var checkCount = 0;
	document.getElementById("del_radius").value = 1;

	for(var i=1; i<rowCount; i++) {
			var row = table.rows[i];
			var chkbox = row.cells[0].childNodes[0];
			chk_box_arr[i-1]=chkbox.checked
	}

	document.getElementById("chk_box_radius").value = chk_box_arr;
	document.getElementById("NumRowsRadius").value = rowCount - 1;

	for(var i=1; i<rowCount; i++) {
			var row = table.rows[i];
			var chkbox = row.cells[0].childNodes[0];
			if(null != chkbox && true == chkbox.checked) {
					checkCount++;
					table.rows[i].style.display = 'none';
			}
	}

	if (checkCount == 0)
	{
			alert("select radius to delete");
	}
	else
	{
	set_visible("tr_warning_radius", true);
	}

}

function CheckIP(ipaddr)
{
	var pattern = /^\d{1,3}(\.\d{1,3}){3}$/;
	if (!pattern.exec(ipaddr))
	{
			return false;
	}
	var aryIP = ipaddr.split('.');

	if (parseInt(aryIP[0]) >= 224 || parseInt(aryIP[0]) == 127 || parseInt(aryIP[0]) == 0)
	{
			return false;
	}
	for(key in aryIP)
	{
			if (parseInt(aryIP[key]) > 255 || parseInt(aryIP[key]) < 0)
			{
					return false;
			}
	}
	return true;
}

function validate_enterprise_param(rad_ip, rad_port, shared_key)
{
	shared_key.value=shared_key.value.replace(/(\")/g, '\"');

	if (rad_ip.value.length == 0)
	{
		alert("Radius IP address cannot be empty");
		return false;
	}
	if (!CheckIP(rad_ip.value))
	{
		alert("Invalid IP address");
		return false;
	}
	if (rad_port.value.length > 0 && isNaN(rad_port.value) == true)
		{
				alert("Invalid Port Number");
				return false;
		}

	if (rad_port.value < 1 || rad_port.value > 65535)
	{
		alert("Allowed port number range is 1-65535");
		return false;
	}
	if (rad_port.value.length == 0)
	{
		alert("Radius port number cannot be empty");
		return false;
	}
	if (shared_key.value.length == 0)
	{
		alert("Shared key cannot be empty");
		return false;
	}
	if (shared_key.value.length < 8 || shared_key.value.length > 128)
	{
		alert("Allowed passphrase is 8 to 127 ASCII characters or 128 Hexadecimal digits");
		return false;
	}
	if ((nonascii.test(shared_key.value)))
	{
		alert("Allowed passphrase is 8 to 127 ASCII characters or 128 Hexadecimal digits");
		return false;
	}

	if (shared_key.value.length == 128 && (nonhex.test(shared_key.value)))
	{
		alert("Allowed passphrase is 8 to 127 ASCII characters or 128 Hexadecimal digits");
		return false;
	}

	return true;
}

function addRadiusRow(tableID)
{
	set_visible('radius_table', true);

	var radius_ip = document.getElementById("txt_radius_ipaddr");
	var radius_port = document.getElementById("txt_radius_port");
	var shared_key = document.getElementById("txt_shared_key");

	if (!validate_enterprise_param(radius_ip, radius_port, shared_key))
	{
		return false;
	}

	var radius_val = radius_ip.value + " " + radius_port.value + " " + shared_key.value;
	document.getElementById("add_radius").value = 1;
	var table = document.getElementById(tableID);

	var rowCount = table.rows.length;
	var row = table.insertRow(rowCount);

	var cell1 = row.insertCell(0);
	var element1 = document.createElement("input");
	element1.type = "checkbox";
	element1.name="chkbox[]";
	cell1.appendChild(element1);

	var cell2 = row.insertCell(1);
	var element2 = document.createElement("input");
	element2.type="text";
	element2.name="txtbox_radius[]";
	element2.value=radius_val;
	element2.readOnly = true;
	cell2.appendChild(element2);

	document.getElementById("NumRowsRadius").value = rowCount;
	document.getElementById("txt_radius_ipaddr").value="";
	document.getElementById("txt_shared_key").value="";

	set_visible("tr_warning_radius", true);
}

function validate_psk()
{
	var encryption = document.getElementById("cmb_encryption").value;
	pw = document.getElementById("txt_passphrase");
	pw.value=pw.value.replace(/(\")/g, '\"');
	var t = document.getElementById("is_psk");
	t.value = -1;
	if (pw.value.length < 8 || pw.value.length > 64)
	{
		if(encryption != "SAE-WPA-PSK") {
			alert("Allowed Passphrase is 8 to 63 ASCII characters or 64 Hexadecimal digits");
		}
		else {
			alert("Allowed Passphrase is 8 to 63 ASCII characters");
		}
		return false;
	}
	if(pw.value.length == 64 && encryption == "SAE-WPA-PSK") {
		alert("Allowed Passphrase is 8 to 63 ASCII characters");
		return false;
	}
	if ((nonascii.test(pw.value)))
	{
		if(encryption != "SAE-WPA-PSK") {
			alert("Allowed Passphrase is 8 to 63 ASCII characters or 64 Hexadecimal digits");
		}
		else {
			alert("Allowed Passphrase is 8 to 63 ASCII characters");
		}
		return false;
	}
	if (pw.value.length == 64 && (nonhex.test(pw.value)) && encryption != "SAE-WPA-PSK")
	{
		alert("Allowed Passphrase is 8 to 63 ASCII characters or 64 Hexadecimal digits");
		return false;
	}

	if (pw.value.length == 64)
	{
		t.value = 1;
	}
	else
	{
		t.value = 0;
	}

	return true;
}

function validate_sta_psk()
{
	var encryption = document.getElementById("cmb_sta_encryption").value;
	pw = document.getElementById("txt_sta_passphrase");
	pw.value=pw.value.replace(/(\")/g, '\"');
	var t = document.getElementById("sta_is_psk");
	t.value = -1;
	if ((pw.value.length < 8 && pw.value.length > 0) || pw.value.length > 64)
	{
		if(encryption != "SAE-WPA-PSK") {
			alert("Allowed Passphrase is 8 to 63 ASCII characters or 64 Hexadecimal digits");
		}
		else {
			alert("Allowed Passphrase is 8 to 63 ASCII characters");
		}
		return false;
	}
	if(pw.value.length == 64 && encryption == "SAE-WPA-PSK") {
		alert("Allowed Passphrase is 8 to 63 ASCII characters");
		return false;
	}
	if ((nonascii.test(pw.value)))
	{
		if(encryption != "SAE-WPA-PSK") {
			alert("Allowed Passphrase is 8 to 63 ASCII characters or 64 Hexadecimal digits");
		}
		else {
			alert("Allowed Passphrase is 8 to 63 ASCII characters");
		}
		return false;
	}
	if (pw.value.length == 64 && (nonhex.test(pw.value)) && encryption != "SAE-WPA-PSK")
	{
		alert("Allowed Passphrase is 8 to 63 ASCII characters or 64 Hexadecimal digits");
		return false;
	}

	if (pw.value.length == 64)
	{
		t.value = 1;
	}
	else
	{
		t.value = 0;
	}

	return true;
}

function validate(action_name)
{
	var curr_hs20_status = "<?php echo $curr_hs20_status; ?>";
	var tmp = document.getElementById("action");
	tmp.value = action_name;

	if (action_name==0)//Save Button
	{
		var no_num= /[^0-9]/;
		var ssid = document.getElementById("txt_essid");
		var sta_ssid = document.getElementById("txt_sta_essid");
		ssid.value=ssid.value.replace(/(\")/g, '\"');
		var cmb_devicemode = document.getElementById("cmb_devicemode");
		var broadcast = document.getElementById("chk_broadcast");
		var bint = document.getElementById("txt_beaconinterval");
		var dtim = document.getElementById("txt_dtimperiod");
		var vlan = document.getElementById("txt_vlan");
		var t = document.getElementById("is_psk");
		var ieee80211r = document.getElementById("chk_ieee80211r");
		var mobility_domain = document.getElementById("txt_mobility_domain");
		var nas_id = document.getElementById("txt_nas_id");
		nas_id.value=nas_id.value.replace(/(\")/g, '\"');
		var r1kh_id = document.getElementById("txt_r1kh_id");
		var cmb_encryption_value = document.mainform.cmb_encryption.value;

		t.value = -1;

		if(mode == cmb_devicemode.value )
		{
			if(!(mode == "Station"  && cmb_encryption_value == "DPP")) {
				if (ssid.value.length < 1 || ssid.value.length > 32)
				{
					alert("SSID must contain between 1 and 32 ASCII characters");
					return false;
				}

				if ((nonascii.test(ssid.value)))
				{
					alert("Only ASCII characters allowed in SSID");
					return false;
				}

				if ((ssid.value[0] == ' ') || (ssid.value[ssid.value.length - 1] == ' '))
				{
					alert("SPACE is not allowed at the start or end of the SSID");
					return false;
				}
			}

			if(cmb_encryption_value != "DPP") {
				/* Validate Passphrase, Radius IPAdress, Radius Port And Shared Key */
				if (document.mainform.cmb_pmf.selectedIndex == 0)
				{
					if (cmb_encryption_value != "NONE" && (mode == "Access point" || mode == "Repeater"))
					{
						gki = document.getElementById("txt_group_key_interval");
						if(((mode=="Access point" && cmb_devicemode.selectedIndex=="0") || (mode=="Repeater" && cmb_devicemode.selectedIndex=="2")) && gki.value == "")
						{
							alert("Please enter Group key interval");
							return false;
						}
						if((mode=="Access point" || mode=="Repeater") && (gki.value != "") && (!(/^\d+$/.test(gki.value))))
						{
							alert("The Group key interval should be natural numbers");
							return false;
						}
						if((mode=="Access point" || mode=="Repeater") && gki.value != "" && (gki.value < 0 || gki.value > 43200))
						{
							alert("Group key interval needs to be between 0 and 43200");
							return false;
						}
						if (gki.value == 0)
						{
							alert("Group key rekeying is going to be disable");
						}
					}
				}
				if (cmb_encryption_value == "WPAand11i" || cmb_encryption_value == "11i" || cmb_encryption_value == "11i_pmf" || cmb_encryption_value == "SAE" || cmb_encryption_value == "SAE-WPA-PSK")
				{
					if ((mode == "Access point" || mode == "Repeater") && curr_hs20_status == 1)
					{
						alert("Security WPA2-PSK is not allowed when HS2.0 is enabled");
						return false;
					}
					if (!validate_psk())
						return false;
				}
				if(cmb_encryption_value == "NONE" && curr_proto != "NONE")
				{
					if (mode == "Access point" || mode == "Repeater")
					{
						if (curr_hs20_status == 1)
						{
							alert("Security NONE-OPEN is not allowed when HS2.0 is enabled");
							return false;
						}
						var tag = confirm('Disable the security?');
						if( tag != true )
						{
							return false;
						}
					}
				}
				if((cmb_encryption_value == "SAE" || cmb_encryption_value == "SAE-WPA-PSK") && mode != "Station") {
					var chk_saegroups = document.getElementsByName("chk_saegroup[]");
					var has_checked = false;
					for(var i=0;i<chk_saegroups.length;i++){
						if(chk_saegroups[i].checked) {
							has_checked = true;
							break;
						}
					}
					if(!has_checked) {
						alert("At least one SAE group should be selected.");
						return false;
					}
				}
				if(cmb_encryption_value == "OWE" && mode != "Station") {
					var chk_owegroups = document.getElementsByName("chk_owegroup[]");
					var has_checked = false;
					for(var i=0;i<chk_owegroups.length;i++){
						if(chk_owegroups[i].checked) {
							has_checked = true;
							break;
						}
					}
					if(!has_checked) {
						alert("At least one OWE group should be selected.");
						return false;
					}
				}
				if(cmb_encryption_value == "WPA2-EAP" || cmb_encryption_value == "WPAand11i-EAP") {
					var radius_table = document.getElementById("radius_table");
					var radius_rows = radius_table.rows;
					var has_radius_config = false;
					for(var i = 1; i < radius_rows.length;i++) {
						if(radius_rows[i].style.display != "none") {
							has_radius_config = true;
							break;
						}
					}
					if(!has_radius_config) {
						alert("At least one Radius Configuration should be set.");
						return false;
					}
				}
			}
			if(cmb_encryption_value == "DPP") {
				if(document.mainform.cmb_dpp_role.value == "configurator") {
					if(document.mainform.txt_dpp_qrcode.value == "") {
						alert("QR Code string can't be empty!");
						return false;
					}
				}
			}

			if (broadcast.checked==false && document.mainform.cmb_encryption.value != "DPP")
			{
				if (mode == "Access point" || mode == "Repeater")
				{
					var tag1=confirm('Disable the broadcast SSID? WPS will also be disabled!');
					if( tag1 != true )
					{
						changetab();
						return false;
					}
				}
			}
			if((mode=="Access point" && cmb_devicemode.selectedIndex=="0" || mode=="Repeater" && cmb_devicemode.selectedIndex=="2") && bint.value == "")
			{
				alert("Please enter beacon interval");
				return false;
			}

			if((mode=="Access point" || mode=="Repeater") && (bint.value != "") && (!(/^\d+$/.test(bint.value))))
			{
				alert("The beacon interval value should be natural numbers");
				return false;
			}
			if((mode=="Access point" || mode=="Repeater" )&& bint.value != "" && (bint.value < 25 || bint.value > 5000))
			{
				alert("Beacon interval needs to be between 25 and 5000");
				return false;
			}

			if((mode=="Access point" && cmb_devicemode.selectedIndex=="0" ||mode=="Repeater" && cmb_devicemode.selectedIndex=="2") && dtim.value == "")
			{
				alert("Please enter DTIM period value");
				return false;
			}

			if((mode=="Access point" || mode=="Repeater" ) && (dtim.value != "") && (!(/^\d+$/.test(dtim.value))))
			{
				alert("The DTIM period value should be natural numbers");
				return false;
			}
			if((mode=="Access point" || mode=="Repeater" ) && (dtim.value != "" )&& ((dtim.value > 15) || (dtim.value < 1)))
			{
				alert("DTIM period needs to be between 1 and 15");
				return false;
			}
			if((mode=="Access point" || mode=="Repeater" ) && (vlan.value != "" ))
			{
				if ((no_num.test(vlan.value)))
				{
					vlan.focus();
					alert("Only Numbers are allowed in VLANID");
					return false;
				}
				if (vlan.value <1 || vlan.value > 4095)
				{
					vlan.focus();
					alert("Vlan ID is only allowed between 1-4095");
					return false;
				}
				var vlan_id=new Array();
				vlan_id[0]="<?php echo $curr_vlan_ids[0]; ?>";
				vlan_id[1]="<?php echo $curr_vlan_ids[1]; ?>";
				vlan_id[2]="<?php echo $curr_vlan_ids[2]; ?>";
				vlan_id[3]="<?php echo $curr_vlan_ids[3]; ?>";
				vlan_id[4]="<?php echo $curr_vlan_ids[4]; ?>";
				vlan_id[5]="<?php echo $curr_vlan_ids[5]; ?>";
				vlan_id[6]="<?php echo $curr_vlan_ids[6]; ?>";
				vlan_id[7]="<?php echo $curr_vlan_ids[7]; ?>";
				for(var vlan_num=1;vlan_num<8;vlan_num++)
				{
					if (vlan.value == vlan_id[vlan_num])
					{
						vlan.focus();
						alert("Vlan ID has been used for WIFI"+vlan_num+" interface. Please enter a different Vlan ID");
						return false;
					}
				}
			}
			if ((mode=="Access point" || mode=="Repeater" ) && ieee80211r.checked == true)
			{
				if((mobility_domain.value.length != 4) || (nonhex.test(mobility_domain.value)))
				{
					alert("Mobility domain ID required. Allowed value is 4 hexadecimal digits");
					return false;
				}
				if((nas_id.value.length > 48) || (nas_id.value.length == 0))
				{
					alert("Nas ID must contain between 1 and 48 characters");
					return false;
				}
				if((r1kh_id.value.length != 0 && r1kh_id.value.length != 12) || (nonhex.test(r1kh_id.value)))
				{
					alert("Allowed R1 key holder value is 12 Hexadecimal digits");
					return false;
				}
			}
			if (mode =="Repeater" && document.mainform.cmb_sta_encryption.value != "DPP")
			{
				var cmb_sta_encryption_value = document.mainform.cmb_sta_encryption.value;
				if ( sta_ssid.value.length > 32)
				{
					alert("SSID must contain between 1 and 32 ASCII characters");
					return false;
				}

				if ((nonascii.test(sta_ssid.value)))
				{
					alert("Only ASCII characters allowed in SSID");
					return false;
				}

				if ((sta_ssid.value[0] == ' ') || (sta_ssid.value[sta_ssid.value.length - 1] == ' '))
				{
					alert("SPACE is not allowed at the start or end of the SSID");
					return false;
				}
				/* Validate Passphrase */
				if (cmb_sta_encryption_value == "11i_pmf" || cmb_sta_encryption_value == "SAE" || cmb_sta_encryption_value == "SAE-WPA-PSK" || cmb_sta_encryption_value == "11i" || cmb_sta_encryption_value == "WPAand11i")
				{
					if (!validate_sta_psk())
						return false;
				}
			}
		}
		document.mainform.submit();
	}
	else if(action_name==1)//Cancel Button
	{
		window.location.href="config_wireless.php";
	}
	else if(action_name==6)//Show AP List Page
	{
		popnew("config_aplist.php");
	}
}

function populate_radius(curr_radius)
{
		var table = document.getElementById("radius_table");

		var rowCount = table.rows.length;
		var row = table.insertRow(rowCount);

		var cell1 = row.insertCell(0);
		var element1 = document.createElement("input");
		element1.type = "checkbox";
		element1.name = "chkbox[]";
		cell1.appendChild(element1);

		var cell2 = row.insertCell(1);
		var element2 = document.createElement("input");
		element2.type="text";
		element2.name="txtbox_radius[]";
		element2.value=curr_radius;
		element2.readOnly = true;
		cell2.appendChild(element2);
}
function onload_event()
{
	//var curr_proto = "<?php echo $curr_proto; ?>";
	var ieee80211r = "<?php echo $curr_ieee80211r; ?>";
	var radius_arr_len = "<?php echo $radius_arr_len; ?>";
	var saegroups = "<?php echo $curr_saegroups; ?>";
	var owegroups = "<?php echo $curr_owegroups; ?>";
	init_menu();
	if (curr_region=="none")
	{
		populate_channellist(0, curr_band);
	}
	else
	{
		if (curr_bw == "20")
		{
			populate_channellist(20, curr_band);
		}
		else if (curr_bw == "40")
		{
			populate_channellist(40, curr_band);
		}
		else if (curr_bw == "80")
		{
			populate_channellist(80, curr_band);
		}
	}
	if (curr_band == "11ac" || curr_band == "11acOnly")
	{
		nss = "<?php echo $curr_nss; ?>";
		populate_11ac_mcslist(curr_bw, nss);
	}
	else if (curr_band == "11na"
			|| curr_band == "11ng"
			|| curr_band == "11nOnly")
	{
		populate_mcslist();
	}
	else
	{
		set_visible('tr_txrate', false);
		set_visible('tr_nss', false);
		if (mode == "Station")
			set_visible('tr_divline',false);
	}

	if (mode == "Station" || curr_band != "11ng" || curr_bw != "40")
		set_visible('tr_sec_channel', false);
	else
		populate_sec_channel_list();

	populate_bandlist();
	populate_bwlist();
	populate_encryptionlist(curr_pmf);
	populate_sta_encryptionlist(curr_sta_pmf);

	set_visible('tr_saegroups', false);
	set_visible('tr_owegroups', false);
	set_visible('tr_sta_saegroup', false);
	set_visible('tr_sta_owegroup', false);
	set_visible('radius_display_table', false);
	set_visible('radius_table', false);
	set_visible('dpp_field', false);
	set_visible('dpp_enrollee_field', false);
	set_visible('tr_group_key_interval', true);
	set_visible('tr_passphrase', true);
	set_visible('dpp_qrcode_field', true);
	set_visible('tr_essid', true);
	set_visible('tr_pmf', true);
	set_disabled('chk_ieee80211r', true);
	update_11r_config_display(false);

	for( var i=0; i<radius_arr_len; i++) {
		populate_radius(radius_arr[i]);
	}

	if (curr_proto == "NONE" || curr_proto == "OWE" || curr_proto == "DPP")
	{
		set_visible('tr_group_key_interval', false);
	}
	if (curr_proto == "NONE" || curr_proto == "" || curr_proto == "WPA2-EAP"
		|| curr_proto == "WPAand11i-EAP" || curr_proto == "OWE" || curr_proto == "DPP")
	{
		set_visible('tr_passphrase', false);
	}
	if (curr_proto == "WPA2-EAP" || curr_proto == "WPAand11i-EAP")
	{
		set_visible('radius_display_table', true);
		set_visible('radius_table', true);
	}
	if (curr_proto == "DPP") {
		set_visible('dpp_field', true);
	}
	if (privilege > 1)
	{
		set_visible('tbc_advanced', false);
	}
	if (mode == "Access point" || mode =="Repeater")
	{
		set_visible("btn_aplist", false);
		if(mode == "Access point")
		{
			set_visible("tr_sta", false);
			set_visible("tr_sta_essid", false);
			set_visible("tr_sta_pmf", false);
			set_visible("tr_sta_encryption", false);
			set_visible("tr_sta_passphrase", false);
		}
		else if(curr_sta_ssid != "")
		{
			set_disabled('cmb_channel', true);
		}

		if (curr_proto != "NONE" || curr_proto != "OWE")
		{
			set_disabled('chk_ieee80211r', false);
			if(ieee80211r == "1")
				update_11r_config_display(true);
		}

		if (curr_proto == "SAE" || curr_proto == "SAE-WPA-PSK")
			set_visible('tr_saegroups', true);

		if (curr_proto == "OWE")
			set_visible('tr_owegroups', true);
	}
	else if (mode == "Station")
	{
		set_visible("cmb_channel", false);
		set_visible("tr_priority", false);
		set_visible("tr_broadcast", false);
		set_visible("tr_beaconinterval", false);
		set_visible("tr_dtimperiod", false);
		set_visible("tr_shortgi", false);
		set_visible("tr_vlan", false);
		set_visible("tr_group_key_interval", false);
		set_visible("tr_frequency_band", false);
		set_visible("chk_ieee80211r", false);
		set_visible("chk_ieee80211r_label", false);
		update_11r_config_display(false);
		set_visible("tr_11v", false);
		set_visible("tr_11k", false);
		set_visible("tr_sta", false);
		set_visible("tr_sta_essid", false);
		set_visible("tr_sta_pmf", false);
		set_visible("tr_sta_encryption", false);
		set_visible("tr_sta_passphrase", false);

		if(curr_proto == "SAE" || curr_proto == "SAE-WPA-PSK")
			set_visible('tr_sta_saegroup', true);

		if(curr_proto == "OWE")
			set_visible('tr_sta_owegroup', true);

		if (curr_proto == "DPP") {
			set_visible("tr_pmf", false);
			set_visible("tr_essid", false);
		}
	}
	if (curr_soniq_role == "slave")
	{
		set_disabled('cmb_pmf', 1);
		set_disabled('cmb_encryption', 1);
		set_disabled('txt_passphrase', 1);
		set_disabled('txt_radius_ipaddr', 1);
		set_disabled('txt_radius_port', 1);
		set_disabled('txt_shared_key', 1);
		set_disabled('txt_essid', 1);
	}

	set_control_value('cmb_devicemode','<?php echo $curr_mode; ?>', 'combox');
	set_control_value('txt_channel','<?php echo $curr_channel_str; ?>', 'text');

	set_control_value('cmb_pmf','<?php echo $curr_pmf; ?>', 'combox');
	set_control_value('cmb_sta_pmf','<?php echo $curr_sta_pmf; ?>', 'combox');
	set_control_value('txt_passphrase',decodeURIComponent("<?php echo rawurlencode($curr_psk); ?>"), 'text');
	set_control_value('txt_radius_ipaddr','<?php echo $curr_radius_ipaddr; ?>', 'text');
	set_control_value('txt_radius_port','<?php echo $curr_radius_port; ?>', 'text');
	set_control_value('txt_shared_key',decodeURIComponent("<?php echo rawurlencode($curr_shared_key); ?>"), 'text');
	set_control_value('cmb_sta_saegroup','<?php echo $curr_sta_saegroups; ?>', 'combox');
	set_control_value('cmb_sta_owegroup','<?php echo $curr_sta_owegroups; ?>', 'combox');
	set_control_value('txt_group_key_interval','<?php echo $curr_group_key_interval; ?>', 'text');
	set_control_value('txt_dpp_qrcode', curr_dpp_qrcode, 'text');
	set_control_value('txt_sta_dpp_qrcode', curr_sta_dpp_qrcode, 'text');

	set_control_value('cmb_priority','<?php echo $curr_priority; ?>', 'combox');
	set_control_value('chk_broadcast','<?php echo $curr_broadcast; ?>', 'checkbox');
	set_control_value('txt_beaconinterval','<?php echo $curr_bintval; ?>', 'text');
	set_control_value('txt_dtimperiod','<?php echo $curr_dtim_period; ?>', 'text');
	set_control_value('chb_shortgi','<?php echo $curr_short_gi; ?>', 'checkbox');
	set_control_value('txt_vlan','<?php echo $curr_vlan; ?>', 'text');
	set_control_value('chk_ieee80211r','<?php echo $curr_ieee80211r; ?>','checkbox');
	set_control_value('txt_mobility_domain','<?php echo $curr_11r_mob_id; ?>', 'text');
	set_control_value('txt_nas_id','<?php echo $curr_11r_nas_id; ?>', 'text');
	set_control_value('txt_r1kh_id','<?php echo $curr_11r_r1kh_id; ?>', 'text');
	set_control_value('chk_ft_over_ds','<?php echo $curr_11r_ft_over_ds; ?>','checkbox');
	set_control_value('chk_11v_btm_cap','<?php echo $curr_11v_btm_cap; ?>','checkbox');
	set_control_value('chk_11k_neigh_repo','<?php echo $curr_11k_neigh_repo; ?>','checkbox');
	set_control_value('chb_soniq','<?php echo $curr_soniq; ?>', 'checkbox');
	set_control_value('txt_soniq','Role:'+'<?php echo $curr_soniq_role; ?>', 'text');

	set_visible("tr_warning_basic", false);
	set_visible("tr_warning_adv", false);
	set_visible("tr_warning_radius", false);

	var chk_saegroups = document.getElementsByName("chk_saegroup[]");
	for(var i=0;i<chk_saegroups.length;i++){
		if(saegroups.indexOf(chk_saegroups[i].value) != -1) {
			chk_saegroups[i].checked = true;
		}
	}
	var chk_owegroups = document.getElementsByName("chk_owegroup[]");
	for(var i=0;i<chk_owegroups.length;i++){
		if(owegroups.indexOf(chk_owegroups[i].value) != -1) {
			chk_owegroups[i].checked = true;
		}
	}
	var qrcode_element = new QRCode(document.getElementById("dpp_qrcode_image"), {width : 147,height : 147});
	qrcode_element.makeCode(curr_dpp_qrcode);
	var sta_qrcode_element = new QRCode(document.getElementById("dpp_sta_qrcode_image"), {width : 147,height : 147});
	sta_qrcode_element.makeCode(curr_sta_dpp_qrcode);
}
</script>

<body class="body" onload="onload_event();" >
	<div class="top">
		<a class="logo" href="./status_device.php">
			<img src="./images/logo.png"/>
		</a>
	</div>
<div class="container" style="height: 840px; ">
	<div class="left">
		<script type="text/javascript">
			createMenu('<?php echo $curr_mode;?>',privilege);
		</script>
	</div>
	<form enctype="multipart/form-data" action="config_wireless.php" id="mainform" name="mainform" method="post">
	<input type="hidden" name="action" id="action" value="action" />
	<div class="right">
		<div class="righttop">CONFIG - WIRELESS</div>
		<div id="TabbedPanels1" class="TabbedPanels">
			<ul class="TabbedPanelsTabGroup">
				<li class="TabbedPanelsTab" tabindex="0">Basic</li>
				<li class="TabbedPanelsTab" tabindex="0" id="tbc_advanced">Advanced</li>
			</ul>
			<div class="TabbedPanelsContentGroup">
				<div class="TabbedPanelsContent" style="height: 761px; ">
					<div class="rightmain">
						<table class="tablemain">
						<tr>
							<td width="40%">Device Mode:</td>
							<td width="60%">
								<select name="cmb_devicemode" class="combox" id="cmb_devicemode" onchange="modechange(this)">
									<option value="Access point">Access Point</option>
									<option value="Station">Station</option>
									<option value="Repeater">Repeater</option>
								</select>
							</td>
						</tr>
						<tr>
							<td class="divline" colspan="2";></td>
						</tr>
						<tr id="tr_essid">
							<td>ESSID:</td>
							<td>
								<input name="txt_essid" type="text" id="txt_essid" class="textbox" value="<?php  echo htmlspecialchars($curr_ssid,ENT_QUOTES); ?>"/>
								<button name="btn_aplist" id="btn_aplist" type="button" onclick="validate(6);" class="button">Scan AP</button>
							</td>
						</tr>
						<tr id="tr_broadcast">
							<td>Broadcast SSID:</td>
							<td>
								<input name="chk_broadcast" id="chk_broadcast" type="checkbox" class="checkbox" value="1" />
							</td>
						</tr>
						<tr>
							<td>Channel:</td>
							<td>
								<select id="cmb_channel" name="cmb_channel" class="combox" onchange="modechange(this)">
								</select>
								<input name="txt_channel" id="txt_channel" type="text" style="width:180px;" disabled class="textbox"/>
							</td>
						</tr>
						<tr id="tr_sec_channel">
							<td>Secondary Channel:</td>
							<td>
								<select id="cmb_sec_channel" name="cmb_sec_channel" class="combox" onchange="modechange(this)">
								</select>
							</td>
						</tr>
						<tr>
							<td class="divline" colspan="2";></td>
						</tr>
						<tr id="tr_pmf">
							<td>PMF:</td>
							<td>
								<select name="cmb_pmf" id="cmb_pmf" class="combox" onchange="modechange(this)">
									<option value="0">Disabled</option>
									<option value="1">Enabled</option>
									<option value="2">Required</option>
								</select>
							</td>
						</tr>
						</table>
						<table class="tablemain">
						<tr>
							<td width="40%">Encryption:</td>
							<td width="40%">
								<select name="cmb_encryption" id="cmb_encryption" class="combox" onchange="modechange(this)">
								</select>
							</td>
							<td>
							<span id="chk_ieee80211r_label">
							<label for "chk_ieee80211r">IEEE 802.11r:</label>
							</td>
							<td>
								<input name="chk_ieee80211r" type="checkbox"  id="chk_ieee80211r" class="checkbox" value="1" onchange="modechange(this);"/>
							</td>
							</span>
						</tr>
						</table>
						<table class="tablemain">
						<tr id="tr_passphrase">
							<td width="40%">Passphrase:</td>
							<td width="60%">
								<input name="txt_passphrase" type="text" id="txt_passphrase" value="<?php  echo htmlspecialchars($curr_psk,ENT_QUOTES); ?>" class="textbox"/><input type="hidden" id="is_psk" name="is_psk" />
							</td>
						</tr>
						</table>
						<table id="dpp_field" class="tablemain" style="display:none">
							<tr>
								<td width="40%">Role:</td>
								<td width="60%">
									<select id="cmb_dpp_role" name="cmb_dpp_role" class="combox" onchange="modechange(this)">
										<option value="enrollee">Enrollee</option>
										<?php  if ($curr_mode != "Station") echo '<option value="configurator">Configurator</option>'; ?>
									</select>
								</td>
							</tr>
							<tr id="dpp_enrollee_field" style="display:none">
								<td width="40%">Enrollee:</td>
								<td width="60%">
									<select id="cmb_dpp_enrollee" name="cmb_dpp_enrollee" class="combox" onchange="modechange(this)">
										<option value="sta">Station</option>
										<option value="ap">Access Point</option>
									</select>
								</td>
							</tr>
							<tr id="dpp_qrcode_field">
								<td width="40%">QR Code:</td>
								<td width="60%">
									<div id="dpp_qrcode_image" style="width:147px; height:147px; margin-left:0px; background-color:red"></div>
									<input id="txt_dpp_qrcode" name="txt_dpp_qrcode" class="textbox" style="margin-top:5px" />
								</td>
							</tr>
						</table>
						<table class="tablemain">
							<tr id="tr_saegroups">
								<td>SAE AP Group:</td>
								<td>
									<input name="chk_saegroup[]" type="checkbox" class="checkbox" value="19"/>19
								</td>
								<td>
									<input name="chk_saegroup[]" type="checkbox" class="checkbox" value="20"/>20
								</td>
								<td>
									<input name="chk_saegroup[]" type="checkbox" class="checkbox" value="21"/>21
								</td>
								<td style="display:none">
									<input name="chk_saegroup[]" type="checkbox" class="checkbox" value="25"/>25
								</td>
								<td style="display:none">
									<input name="chk_saegroup[]" type="checkbox" class="checkbox" value="26"/>26
								</td>
							</tr>
						</table>
						<table class="tablemain">
							<tr id="tr_owegroups">
								<td width="20%">OWE AP Group:<br></td>
								<td>
									<input name="chk_owegroup[]" type="checkbox" class="checkbox" value="19"/>19
								</td>
								<td>
									<input name="chk_owegroup[]" type="checkbox" class="checkbox" value="20"/>20
								</td>
								<td>
									<input name="chk_owegroup[]" type="checkbox" class="checkbox" value="21"/>21
								</td>
							</tr>
						</table>
						<table class="tablemain">
						<tr id="tr_sta_saegroup">
							<td width="40%">SAE Station Group:</td>
							<td width="60%">
								<select name="cmb_sta_saegroup" id="cmb_sta_saegroup" class="combox" onchange="modechange(this)">
									<option value="19">Group 19</option>
									<option value="20">Group 20</option>
									<option value="21">Group 21</option>
									<option value="25" style="display:none">Group 25</option>
									<option value="26" style="display:none">Group 26</option>
								</select>
							</td>
						</tr>
						</table>
						<table class="tablemain">
						<tr id="tr_sta_owegroup">
							<td width="40%">OWE Station Group:</td>
							<td width="60%">
								<select name="cmb_sta_owegroup" id="cmb_sta_owegroup" class="combox" onchange="modechange(this)">
									<option value="19">Group 19</option>
									<option value="20">Group 20</option>
									<option value="21">Group 21</option>
								</select>
							</td>
						</tr>
						</table>
						<table id="radius_display_table">
							<td><font size="2">Radius IP:</font></td>
							<td>
								<input name="txt_radius_ipaddr" type="text" id="txt_radius_ipaddr" class="textbox" style="width:80px;"/>
							</td>
							<td><font size ="2">Radius Port:</font></td>
							<td>
								<input name="txt_radius_port" type="text" id="txt_radius_port" class="textbox" style="width:60px;"/>
							</td>
							<td><font size ="2">Shared Key:</font></td>
							<td>
								<input name="txt_shared_key" type="text" id="txt_shared_key" class="textbox" style="width:80px;"/>
							</td>
							<td><button name="btn_add" id="btn_add" type="button" onclick="addRadiusRow('radius_table');" class="button" style="width:60px;">ADD</button></td>
							<td><button name="btn_del" id="btn_del" type="button" onclick="deleteRadiusRow('radius_table');" class="button" style="width:40px;">DEL</button></td>
							<table id="radius_table" width="350px" border="1">
								<td width="20%">option</td>
								<td width="20%">IP Port shared_key</td>
							</table>
							<input name="NumRowsRadius" id="NumRowsRadius" type="hidden"/>
							<input name="chk_box_radius" id="chk_box_radius" type="hidden"/>
							<input name="add_radius" id="add_radius" type="hidden"/>
							<input name="del_radius" id="del_radius" type="hidden"/>
						</table>
					<table class="tablemain">
						<tr id="tr_group_key_interval">
							<td width="40%">Group Key interval(in sec):</td>
							<td width="60%">
								<input name="txt_group_key_interval" type="text" id="txt_group_key_interval" value="<?php  echo htmlspecialchars($curr_group_key_interval,ENT_QUOTES); ?>" class="textbox"/>
							</td>
						</tr>
					</table>
					<table class="tablemain">
					<tr id="tr_ln">
						<td class="divline" colspan="5";></td>
					</tr>
					<tr>
						<td>
						<span id="txt_mobility_domain_label"><label for "txt_mobility_domain">Mobility Domain:</br></label>
						<input name="txt_mobility_domain" style="width:46px" type="text" id="txt_mobility_domain" class="textbox" value="<?php echo $curr_11r_mob_id; ?>"/>
						</td>
						</span>
						<td>
						<span id="txt_nas_id_label"><label for "txt_nas_id">Nas ID:</br></label>
						<input name="txt_nas_id" type="text" id="txt_nas_id" class="textbox" value="<?php echo $curr_11r_nas_id; ?>"/>
						</td>
						</span>
						<td>
						<span id="txt_r1kh_id_label"><label for "txt_r1kh_id">R1 Key Holder:</br></label>
						<input name="txt_r1kh_id" type="text" id="txt_r1kh_id" class="textbox" value="<?php echo $curr_11r_r1kh_id; ?>"/>
						</td>
						</span>
						<td>
						<span id="chk_ft_over_ds_label"><label for "chk_ft_over_ds">FT over DS:</br></label>
						<input name="chk_ft_over_ds" type="checkbox"  id="chk_ft_over_ds" class="checkbox" value="1"/>
						</td>
						</span>
					</tr>
					</table>
					<table class="tablemain">
						<tr id="tr_sta">
							<td class="divline" colspan="2";></td>
						</tr>
						<tr id="tr_sta_essid">
							<td width="40%">STA ESSID:</td>
							<td width="60%">
								<input name="txt_sta_essid" type="text" id="txt_sta_essid" class="textbox" value="<?php  echo htmlspecialchars($curr_sta_ssid,ENT_QUOTES); ?>"/>
								<button name="btn_sta_aplist" id="btn_sta_aplist" type="button" onclick="validate(6);" class="button">Scan AP</button>
							</td>
						</tr>
						<tr id="tr_sta_pmf">
							<td>STA PMF:</td>
							<td>
								<select name="cmb_sta_pmf" id="cmb_sta_pmf" class="combox" onchange="modechange(this)">
									<option value="0">Disabled</option>
									<option value="1">Enabled</option>
									<option value="2">Required</option>
								</select>
							</td>
						</tr>
						<tr id="tr_sta_encryption">
							<td width="40%">STA Encryption:</td>
							<td width="40%">
								<select name="cmb_sta_encryption" id="cmb_sta_encryption" class="combox" onchange="modechange(this)">
								</select>
							</td>
							<td>
						</tr>
						<tr id="tr_sta_passphrase">
							<td width="40%">STA Passphrase:</td>
							<td width="60%">
								<input name="txt_sta_passphrase" type="text" id="txt_sta_passphrase" value="<?php  echo htmlspecialchars($curr_sta_psk,ENT_QUOTES); ?>" class="textbox"/><input type="hidden" id="sta_is_psk" name="is_sta_psk" />
							</td>
						</tr>
						<tr id="dpp_sta_qrcode_field" style="display:none">
							<td width="40%">QR Code:</td>
							<td width="60%">
								<div id="dpp_sta_qrcode_image" style="width:147px; height:147px; margin-top:15px; margin-left:0px"></div>
								<input id="txt_sta_dpp_qrcode" class="textbox" style="margin-top:5px"/>
							</td>
						</tr>
					</table>
					<table class="tablemain">
						<tr>
							<td class="divline" colspan="2";></td>
						</tr>
						<tr id="tr_warning_radius">
							<td colspan="2"; style="color:red; font-size:smaller;">*To apply the changes, click save button*</td>
						</tr>
						<tr id="tr_warning_basic">
							<td colspan="2"; style="color:red; font-size:smaller;">*The system will reboot to apply the change*</td>
						</tr>
					</table>
						<div class="rightbottom">
							<?php
							 /* Save Button is disabled if station is connected in WPA mode */
							if (($curr_mode == "Station") && ($curr_proto == "WPA"))
							{
								echo "<button name=\"btn_save_basic\" id=\"btn_save_basic\" type=\"button\" disabled=true class=\"button\">Save</button>";
								echo"<button name=\"btn_cancel_basic\" id=\"btn_cancel_basic\" type=\"button\" onclick=\"validate(1);\"  class=\"button\">Cancel</button>";
								echo "<br><br><font size=\"2\"><span style=\"color:Maroon\"> Save option is disabled for WPA Security mode </span></br></br>";
							}
							else
							{
								echo "<button name=\"btn_save_basic\" id=\"btn_save_basic\" type=\"button\" onclick=\"validate(0)\"  class=\"button\">Save</button>";
								 echo"<button name=\"btn_cancel_basic\" id=\"btn_cancel_basic\" type=\"button\" onclick=\"validate(1);\"  class=\"button\">Cancel</button>";

							}
							?>
						</div>
					</div>
				</div>
				<div class="TabbedPanelsContent" style="height: 761px; ">
					<div class="rightmain">
						<table class="tablemain">
						<tr id="tr_frequency_band">
							<td width="40%">Frequency Band:</td>
							<td width="60%">
								<select name="cmb_frequencyband" class="combox" id="cmb_frequencyband" onchange="modechange(this)">
								</select>
							</td>
						</tr>
						<tr>
							<td width="40%">Wireless Band:</td>
							<td width="60%">
								<select name="cmb_wirelessmode" class="combox" id="cmb_wirelessmode" onchange="modechange(this)">
								</select>
							</td>
						</tr>
						<tr>
							<td>Bandwidth:</td>
							<td>
								<select name="cmb_bandwidth" id="cmb_bandwidth"  class="combox" onchange="modechange(this)">
								</select>
							</td>
						</tr>
						<tr id ="tr_divline">
							<td class="divline" colspan="2";></td>
						</tr>
						<tr id="tr_nss">
							<td>NSS:</td>
							<td>
								<select name="cmb_nss" id="cmb_nss"  class="combox" onchange="modechange(this)">
								</select>
							</td>
						</tr>
						<tr id="tr_txrate">
							<td>TX Rate:</td>
							<td>
								<select name="cmb_txrate" class="combox" id="cmb_txrate">
								</select>
							</td>
						</tr>
						<tr id="tr_priority">
							<td>Priority:</td>
							<td>
								<select name="cmb_priority" id="cmb_priority"  class="combox">
									<option value="0">0</option>
									<option value="1">1</option>
									<option value="2">2</option>
									<option value="3">3</option>
								</select>
							</td>
						</tr>
						<tr id="tr_beaconinterval">
							<td>Beacon Interval (in ms):</td>
							<td>
								<input name="txt_beaconinterval" type="text" id="txt_beaconinterval" class="textbox" />
							</td>
						</tr>
						<tr id="tr_dtimperiod">
							<td>DTIM Period:</td>
							<td>
								<input name="txt_dtimperiod" type="text" id="txt_dtimperiod" class="textbox" />
							</td>
						</tr>
						<tr id="tr_shortgi">
							<td>Short GI:</td>
							<td>
								<input name="chb_shortgi" type="checkbox" id="chb_shortgi" value="1" />
							</td>
						</tr>
						<tr id="tr_vlan">
							<td>VLAN:</td>
							<td>
								<input name="txt_vlan" type="text" id="txt_vlan" class="textbox" />
							</td>
						</tr>
						<tr id="tr_11v">
							<td>11v BSS Transition Capability:</td>
							<td>
								<input name="chk_11v_btm_cap" type="checkbox"  id="chk_11v_btm_cap" class="checkbox" value="1"/>
							</td>
						</tr>
						<tr id="tr_11k">
							<td>11k Neighbour Report:</td>
							<td>
								<input name="chk_11k_neigh_repo" type="checkbox"  id="chk_11k_neigh_repo" class="checkbox" value="1"/>
							</td>
						</tr>
						<tr>
							<td class="divline" colspan="2";></td>
						</tr>
						<tr>
							<td>SONiQ:</td>
							<td>
								<input name="chb_soniq" type="checkbox" id="chb_soniq" onchange="modechange(this)"/>
								<input name="txt_soniq" id="txt_soniq" type="text" style="width:180px;" disabled class="textbox"/>
							</td>
						</tr>
						<tr>
							<td class="divline" colspan="2";></td>
						</tr>
						<tr id="tr_warning_adv">
							<td colspan="2"; style="color:red; font-size:smaller;">*The system will reboot to apply the change*</td>
						</tr>
					</table>
						<div class="rightbottom">
							<button name="btn_save_adv" id="btn_save_basic" type="button" onclick="validate(0);"  class="button">Save</button>
							<button name="btn_cancel_adv" id="btn_cancel_basic" type="button" onclick="validate(1);" class="button">Cancel</button>
						</div>
					</div>
				</div>
			</div>
		</div>
	</div>
	<input type="hidden" name="csrf_token" value="<?php echo get_session_token(); ?>" />
	</form>
<script type="text/javascript">
var TabbedPanels1 = new Spry.Widget.TabbedPanels("TabbedPanels1");
function changetab()
{
	TabbedPanels1.showPanel(1);
}
</script>
</div>

<div class="bottom">
	<a href="help/aboutus.php">About Quantenna</a> |  <a href="help/contactus.php">Contact Us</a> | <a href="help/privacypolicy.php">Privacy Policy</a> | <a href="help/terms.php">Terms of Use</a> | <a href="help/h_wireless_conf.php">Help</a><br />
	<div><?php echo $str_copy ?></div>
</div>
</body>
</html>

