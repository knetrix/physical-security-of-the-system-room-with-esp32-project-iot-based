var txt = msg.payload
var obj = JSON.parse(txt);

sicaklik=parseFloat(obj.sicaklikSensor);
nem=parseFloat(obj.nemSensor);
gaz=parseFloat(obj.gazSensor);
alarm=parseFloat(obj.alarmDurumu);

pir=parseFloat(obj.pırDurumu);
telegramkomut=parseFloat(obj.telegramKapiKomut);
kapi=parseFloat(obj.kapiDurumu);

msg.payload=[{
	measurement: "AlarmDurum",
	fields:{
		Sicaklik:sicaklik,
		Nem:nem,
		Gaz:gaz,
	Alarm:alarm
	}
},
	{
	measurement: "KapiDurum",
	fields:{
		Pir:pir,
		Telegramkomut:telegramkomut,
		Kapi:kapi
	}
}];
return msg;