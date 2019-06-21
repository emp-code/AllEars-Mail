function AllEars() {
// Private
	const _serverPkHex = "0f4d188b9cd0b9a675d947d34eee8dd119522736f498fdc137dd70cec9494d5a"; // Server public key in hex

	// These are just informational, the server enforces the real limits
	// [Level0Limit, Level1Limit, ...]
	const _maxAddressNormal = [0, 0, 3, 30, 250];
	const _maxAddressShield = [0, 5, 25, 125, 250];

	var _userKeys;

	var _userLevel = 0;
	var _userAddress = [];
	var _intMsg = [];

	function _NewIntMsg(sml, ts, from, to, title, body) {
		this.senderMemberLevel = sml;
		this.timestamp = ts;
		this.from = from;
		this.to = to;
		this.title = title;
		this.body = body;
	}

	function _NewAddress(addr, hash, decoded, isShield, accInt, spk, accExt, gk) {
		this.address = addr;
		this.hash = hash;
		this.decoded = decoded;
		this.isShield = isShield;
		this.acceptIntMsg = accInt;
		this.sharePk = spk;
		this.acceptExtMsg = accExt;
		this.useGatekeeper = gk;
	}

	var _FetchBinary = function(url, postData, cb) {
		let r=new XMLHttpRequest();
		r.responseType = "arraybuffer";

		r.onreadystatechange=function(){
			if (r.readyState == 4 && typeof(cb) === "function") {
				const arrayBuffer = r.response;
				if (arrayBuffer) {
					const byteArray = new Uint8Array(arrayBuffer);
					cb(r.status, byteArray);
				}
			}
		}

		r.open("POST", url);
		r.send(postData);
	}

	var _FetchEncrypted = function(url, cleartext, nacl, callback) {
		_FetchBinary("/web/nonce", _userKeys.boxPk, function(httpStatus, nonce) {
			if (httpStatus != 200) return callback(httpStatus);

			const postBox = nacl.crypto_box(cleartext, nonce, nacl.from_hex(_serverPkHex), _userKeys.boxSk);
			let postMsg = new Uint8Array(_userKeys.boxPk.length + postBox.length);
			postMsg.set(_userKeys.boxPk);
			postMsg.set(postBox, _userKeys.boxPk.length);

			_FetchBinary(url, postMsg, callback);
		});
	}

	var _BitSet = function(num, bit) {
		return num | 1<<bit;
	}

	var _BitClear = function(num, bit) {
		return num & ~(1<<bit);
	}

	var _BitTest = function(num, bit) {
		return ((num>>bit) % 2 != 0);
	}

	var _GetBit = function(byteArray, bitNum) {
		const skipBytes = Math.floor(bitNum / 8.0);
		const skipBits = bitNum % 8;

		return _BitTest(byteArray[skipBytes], skipBits);
	}

	var _DecodeAddress = function(byteArray, start) {
		const sixBitTable = "|0123456789abcdefghijklmnopqrstuvwxyz.-@????????????????????????";
		const skip = start * 8;

		let decoded = "";

		for (let i = 0; i < 24; i++) {
			let num = 0;

			if (_GetBit(byteArray, skip + i*6 + 0)) num +=  1;
			if (_GetBit(byteArray, skip + i*6 + 1)) num +=  2;
			if (_GetBit(byteArray, skip + i*6 + 2)) num +=  4;
			if (_GetBit(byteArray, skip + i*6 + 3)) num +=  8;
			if (_GetBit(byteArray, skip + i*6 + 4)) num += 16;
			if (_GetBit(byteArray, skip + i*6 + 5)) num += 32;

			decoded = decoded + sixBitTable[num];
		}

		const end = decoded.indexOf('|');
		if (end == -1) return decoded;

		return decoded.substring(0, end);
	}

	var _DecodeOwnAddress = function(byteArray, start, nacl) {
		let decoded = _DecodeAddress(byteArray, start);

		for (let i = 0; i < _userAddress.length; i++) {
			if (_userAddress[i].isShield) continue;

			if (decoded == _DecodeAddress(_userAddress[i].address, 0)) return decoded;
		}

		return nacl.to_hex(byteArray.slice(start, start + 18));
	}

	var _GetAddressCount = function(isShield) {
		let count = 0;

		for (i = 0; i < _userAddress.length; i++) {
			if (_userAddress[i].isShield == isShield) count++;
		}

		return count;
	}

	var _MakeAddrData = function() {
		let addrData = new Uint8Array(_userAddress.length * 27);

		for (i = 0; i < _userAddress.length; i++) {
			addrData[i*27] = _userAddress[i].isShield      ? _BitSet(addrData[i*27], 0) : _BitClear(addrData[i*27], 0);
			addrData[i*27] = _userAddress[i].acceptIntMsg  ? _BitSet(addrData[i*27], 1) : _BitClear(addrData[i*27], 1);
			addrData[i*27] = _userAddress[i].sharePk       ? _BitSet(addrData[i*27], 2) : _BitClear(addrData[i*27], 2);
			addrData[i*27] = _userAddress[i].acceptExtMsg  ? _BitSet(addrData[i*27], 3) : _BitClear(addrData[i*27], 3);
			addrData[i*27] = _userAddress[i].useGatekeeper ? _BitSet(addrData[i*27], 4) : _BitClear(addrData[i*27], 4);
			addrData[i*27] = _BitClear(addrData[i*27], 5);
			addrData[i*27] = _BitClear(addrData[i*27], 6);
			addrData[i*27] = _BitClear(addrData[i*27], 7);

			addrData.set(_userAddress[i].address, i * 27 + 1);
			addrData.set(_userAddress[i].hash, i * 27 + 19);
		}

		return addrData;
	}

// Public
	this.GetAddress = function(num) {return _userAddress[num].decoded;}
	this.IsAddressShield = function(num) {return _userAddress[num].isShield;}
	this.IsAddressAcceptIntMsg = function(num) {return _userAddress[num].acceptIntMsg;}
	this.IsAddressAcceptExtMsg = function(num) {return _userAddress[num].acceptExtMsg;}
	this.IsAddressSharePk      = function(num) {return _userAddress[num].sharePk;}
	this.IsAddressGatekeeper   = function(num) {return _userAddress[num].useGatekeeper;}

	this.SetAddressAcceptIntMsg = function(num, val) {_userAddress[num].acceptIntMsg = val;}
	this.SetAddressAcceptExtMsg = function(num, val) {_userAddress[num].acceptExtMsg = val;}
	this.SetAddressSharePk      = function(num, val) {_userAddress[num].sharePk = val;}
	this.SetAddressGatekeeper   = function(num, val) {_userAddress[num].useGatekeeper = val;}

	this.GetAddressCount = function() {return _userAddress.length;}
	this.GetAddressCountNormal = function() {return _GetAddressCount(false);}
	this.GetAddressCountShield = function() {return _GetAddressCount(true);}

	this.GetUserLevel = function() {return _userLevel;}
	this.GetAddressLimitNormal = function() {return _maxAddressNormal[_userLevel];}
	this.GetAddressLimitShield = function() {return _maxAddressShield[_userLevel];}

	this.GetIntMsgCount = function() {return _intMsg.length;}
	this.GetIntMsgLevel = function(num) {return _intMsg[num].senderMemberLevel;}
	this.GetIntMsgTime  = function(num) {return _intMsg[num].timestamp;}
	this.GetIntMsgFrom  = function(num) {return _intMsg[num].from;}
	this.GetIntMsgTo    = function(num) {return _intMsg[num].to;}
	this.GetIntMsgTitle = function(num) {return _intMsg[num].title;}
	this.GetIntMsgBody  = function(num) {return _intMsg[num].body;}

	this.SetKeys = function(skey_hex) { nacl_factory.instantiate(function (nacl) {
		_userKeys=nacl.crypto_box_keypair_from_raw_sk(nacl.from_hex(skey_hex));
	}); }

	this.Login = function(callback) { nacl_factory.instantiate(function (nacl) {
		_FetchEncrypted("/web/login", nacl.encode_utf8("AllEars:Web.Login"), nacl, function(httpStatus, byteArray) {
			if (httpStatus != 200) return callback(false);

			const addrDataSize_bytes = byteArray.slice(0, 2).buffer;
			const addrDataSize = new Uint16Array(addrDataSize_bytes)[0];

			_userLevel = byteArray[2];
			const addrData = nacl.crypto_box_seal_open(byteArray.slice(4, 4 + addrDataSize), _userKeys.boxPk, _userKeys.boxSk);

			// Empty the arrays
			while (_userAddress.length > 0) _userAddress.pop();

			for (i = 0; i < (addrData.length / 27); i++) {
				const isShield      = _BitTest(addrData[i * 27], 0);
				const acceptIntMsg  = _BitTest(addrData[i * 27], 1);
				const sharePk       = _BitTest(addrData[i * 27], 2);
				const acceptExtMsg  = _BitTest(addrData[i * 27], 3);
				const useGatekeeper = _BitTest(addrData[i * 27], 4);
				const addr = addrData.slice(i * 27 + 1, i * 27 + 19); // Address, 18 bytes
				const hash = addrData.slice(i * 27 + 19, i * 27 + 27); // Hash, 8 bytes
				const decoded = isShield ? nacl.to_hex(addr) : _DecodeAddress(addr, 0);

				_userAddress[i] = new _NewAddress(addr, hash, decoded, isShield, acceptIntMsg, sharePk, acceptExtMsg, useGatekeeper);
			}

			// Messages
			let msgStart = 4 + addrDataSize;
			for (let i = 0; i < byteArray[3]; i++) {
				// TODO: Detect message type and support extMsg
				const msgKilos = byteArray[msgStart] + 1;

				// HeadBox
				const msgHeadBox = byteArray.slice(msgStart + 1, msgStart + 90); // 1 + 41 + 48
				const msgHead = nacl.crypto_box_seal_open(msgHeadBox, _userKeys.boxPk, _userKeys.boxSk);

				let im_sml = 0;
				if (_BitTest(msgHead[0], 0)) im_sml++;
				if (_BitTest(msgHead[0], 1)) im_sml += 2;

				const u32bytes = msgHead.slice(1, 5).buffer;
				const im_ts = new Uint32Array(u32bytes)[0];

				const im_from = (_BitTest(msgHead[0], 7)) ? nacl.to_hex(msgHead.slice(5, 23)) : _DecodeAddress(msgHead, 5, nacl);
				const im_to   = _DecodeOwnAddress(msgHead, 23, nacl);

				// BodyBox
				const bbSize = msgKilos * 1024 + 50;
				const bbStart = msgStart + 90;

				const msgBodyBox = byteArray.slice(bbStart, bbStart + bbSize);
				const msgBodyFull = nacl.crypto_box_seal_open(msgBodyBox, _userKeys.boxPk, _userKeys.boxSk);

				const u16bytes = msgBodyFull.slice(0, 2).buffer;
				const padAmount = new Uint16Array(u16bytes)[0];
				const msgBody = msgBodyFull.slice(2, msgBodyFull.length - padAmount);

				const msgBodyUtf8 = nacl.decode_utf8(msgBody);
				const firstLf = msgBodyUtf8.indexOf('\n');
				const im_title=msgBodyUtf8.slice(0, firstLf);
				const im_body=msgBodyUtf8.slice(firstLf + 1);

				_intMsg[i] = new _NewIntMsg(im_sml, im_ts, im_from, im_to, im_title, im_body);
				msgStart += (msgKilos * 1024) + 140; // 48*2+41+2+1=136
			}

			callback(true);
		});
	}); }

	this.Send = function(msgFrom, msgTo, msgTitle, msgBody, callback) { nacl_factory.instantiate(function (nacl) {
		const cleartext = nacl.encode_utf8(msgFrom + '\n' + msgTo + '\n' + msgTitle + '\n' + msgBody);

		_FetchEncrypted("/web/send", cleartext, nacl, function(httpStatus, byteArray) {
			if (httpStatus == 204)
				callback(true);
			else
				callback(false);
		});
	}); }

	this.DeleteAddress = function(num, callback) { nacl_factory.instantiate(function (nacl) {
		const hash = _userAddress[num].hash;
		_userAddress.splice(num, 1);

		const boxAddrData = nacl.crypto_box_seal(_MakeAddrData(), _userKeys.boxPk);
		let postData = new Uint8Array(8 + boxAddrData.length);
		postData.set(hash);
		postData.set(boxAddrData, 8);

		_FetchEncrypted("/web/addr/del", postData, nacl, function(httpStatus, byteArray) {
			if (httpStatus == 204)
				callback(true);
			else
				callback(false);
		});
	}); }

	this.AddAddress = function(addr, callback) { nacl_factory.instantiate(function (nacl) {
		_FetchEncrypted("/web/addr/add", nacl.encode_utf8(addr), nacl, function(httpStatus, byteArray) {
			if (httpStatus != 200) return callback(false);

			_userAddress[_userAddress.length] = new _NewAddress(byteArray.slice(8), byteArray.slice(0, 8), addr, false, false, false, false, true);
			const boxAddrData = nacl.crypto_box_seal(_MakeAddrData(), _userKeys.boxPk);

			_FetchEncrypted("/web/addr/upd", boxAddrData, nacl, function(httpStatus, byteArray) {
				if (httpStatus == 204)
					return callback(true);
				else
					return callback(false);
			});
		});
	}); }

	this.AddShieldAddress = function(callback) { nacl_factory.instantiate(function (nacl) {
		_FetchEncrypted("/web/addr/add", nacl.encode_utf8("SHIELD"), nacl, function(httpStatus, byteArray) {
			if (httpStatus != 200) return callback(false);

			_userAddress[_userAddress.length] = new _NewAddress(byteArray.slice(8), byteArray.slice(0, 8), nacl.to_hex(byteArray.slice(8)), true, false, false, false, true);
			const boxAddrData = nacl.crypto_box_seal(_MakeAddrData(), _userKeys.boxPk);

			_FetchEncrypted("/web/addr/upd", boxAddrData, nacl, function(httpStatus, byteArray) {
				if (httpStatus == 204)
					return callback(true);
				else
					return callback(false);
			});
		});
	}); }

	this.SaveAddressData = function(callback) { nacl_factory.instantiate(function (nacl) {
		const boxAddrData = nacl.crypto_box_seal(_MakeAddrData(), _userKeys.boxPk);

		_FetchEncrypted("/web/addr/upd", boxAddrData, nacl, function(httpStatus, byteArray) {
			if (httpStatus == 204)
				return callback(true);
			else
				return callback(false);
		});
	}); }
}
