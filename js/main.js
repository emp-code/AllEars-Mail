ae=new AllEars();

document.getElementById("btn_signin").addEventListener("click", function(){
	// All-Ears needs to be provided with the user's secret key in order to log in
	ae.SetKeys(document.getElementById('txt_skey').value);

	ae.Login();

	// Continue in functions named allears_onLoginSuccess() and allears_onLoginFailure()
});

function tsToISO8601(ts){
	const dt = new Date(ts * 1000);
	const dt_Y = dt.getUTCFullYear();
	const dt_m = dt.getUTCMonth()   < 10 ? '0' + dt.getUTCMonth()   : dt.getUTCMonth();
	const dt_d = dt.getUTCDate()    < 10 ? '0' + dt.getUTCDate()    : dt.getUTCDate();
	const dt_H = dt.getUTCHours()   < 10 ? '0' + dt.getUTCHours()   : dt.getUTCHours();
	const dt_M = dt.getUTCMinutes() < 10 ? '0' + dt.getUTCMinutes() : dt.getUTCMinutes();
	const dt_S = dt.getUTCSeconds() < 10 ? '0' + dt.getUTCSeconds() : dt.getUTCSeconds();
	return dt_Y + '-' + dt_m + '-' + dt_d + 'T' + dt_H + ':' + dt_M + ':' + dt_S + 'Z';
}

function addOptAddr(addr, isShield) {
	const addrTable = document.getElementById("tbody_opt_addr");
	let row = addrTable.insertRow(-1);
	let cellAddr = row.insertCell(-1);
	let cellChk1 = row.insertCell(-1);
	let cellChk2 = row.insertCell(-1);
	let cellChk3 = row.insertCell(-1);
	let cellChk4 = row.insertCell(-1);

	cellAddr.textContent=addr;
	if (isShield) cellAddr.className="mono";

	cellChk1.innerHTML = "<input type=\"checkbox\">";
	cellChk2.innerHTML = "<input type=\"checkbox\">";
	cellChk3.innerHTML = "<input type=\"checkbox\">";
	cellChk4.innerHTML = "<input type=\"checkbox\">";
}

// Called on a successful login
function allears_onLoginSuccess() {
	console.log("Logged in successfully");

	document.getElementById("div_login").style.display="none";
	document.getElementById("div_loggedin").style.display="inline";

	// Normal addresses
	let select=document.getElementById("send_from");
	for (let i = 0; i < ae.GetAddressCountNormal(); i++) {
		let opt = document.createElement("option");
		opt.value = ae.GetAddressNormal(i);
		opt.textContent = ae.GetAddressNormal(i) + "@allears.test";
		select.appendChild(opt);

		addOptAddr(ae.GetAddressNormal(i), false);
	}

	// Shield addresses
	for (let i = 0; i < ae.GetAddressCountShield(); i++) {
		let opt = document.createElement("option");
		opt.value = ae.GetAddressShield(i);
		opt.textContent = ae.GetAddressShield(i) + "@allears.test";
		select.appendChild(opt);

		addOptAddr(ae.GetAddressShield(i), true);
	}

	// Messages
	for (let i = 0; i < ae.GetIntMsgCount(); i++) {
		const table = document.getElementById("tbody_inbox");

		let row = table.insertRow(-1);
		let cellTime  = row.insertCell(-1);
		let cellTitle = row.insertCell(-1);
		let cellFrom  = row.insertCell(-1);
		let cellTo    = row.insertCell(-1);

		cellTime.textContent = tsToISO8601(ae.GetIntMsgTime(i));
		cellTitle.textContent = ae.GetIntMsgTitle(i);
		cellFrom.textContent = ae.GetIntMsgFrom(i);
		cellTo.textContent = ae.GetIntMsgTo(i);

		row.addEventListener("click", function(){
			document.getElementById("btn_toinbox").disabled="";
			document.getElementById("btn_towrite").disabled="";
			document.getElementById("btn_tosettings").disabled="";

			document.getElementById("div_inbox").style.display="none";
			document.getElementById("div_write").style.display="none";
			document.getElementById("div_settings").style.display="none";
			document.getElementById("div_readmsg").style.display="inline";

			document.getElementById("readmsg_title").textContent = ae.GetIntMsgTitle(i);
			document.getElementById("readmsg_from").textContent = ae.GetIntMsgFrom(i);
			document.getElementById("readmsg_to").textContent = ae.GetIntMsgTo(i);
			document.getElementById("readmsg_body").textContent = ae.GetIntMsgBody(i);
			document.getElementById("readmsg_level").textContent = ae.GetIntMsgLevel(i);
		});
	}
}

// Called on a failed login
function allears_onLoginFailure() {
	console.log("Failed to log in");
}

document.getElementById("btn_send").addEventListener("click", function(){
	ae.Send(document.getElementById("send_from").value, document.getElementById("send_to").value, document.getElementById("send_title").value, document.getElementById("send_body").value);

	// Continue in functions named allears_onSendSuccess() and allears_onSendFailure()
});

function allears_onSendSuccess() {
	console.log("Message sent");
}

function allears_onSendFailure() {
	console.log("Failed to send message");
}

// Menu
document.getElementById("btn_toinbox").addEventListener("click", function(){
	document.getElementById("btn_toinbox").disabled="disabled";
	document.getElementById("btn_towrite").disabled="";
	document.getElementById("btn_tosettings").disabled="";

	document.getElementById("div_readmsg").style.display="none";
	document.getElementById("div_settings").style.display="none";
	document.getElementById("div_write").style.display="none";
	document.getElementById("div_inbox").style.display="inline";
});

document.getElementById("btn_towrite").addEventListener("click", function(){
	document.getElementById("btn_toinbox").disabled="";
	document.getElementById("btn_towrite").disabled="disabled";
	document.getElementById("btn_tosettings").disabled="";

	document.getElementById("div_readmsg").style.display="none";
	document.getElementById("div_inbox").style.display="none";
	document.getElementById("div_settings").style.display="none";
	document.getElementById("div_write").style.display="inline";
});

document.getElementById("btn_tosettings").addEventListener("click", function(){
	document.getElementById("btn_toinbox").disabled="";
	document.getElementById("btn_towrite").disabled="";
	document.getElementById("btn_tosettings").disabled="disabled";

	document.getElementById("div_readmsg").style.display="none";
	document.getElementById("div_inbox").style.display="none";
	document.getElementById("div_write").style.display="none";
	document.getElementById("div_settings").style.display="inline";
});
