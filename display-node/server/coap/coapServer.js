/**
 * Created by rui on 2/17/17.
 */
let coap             = require('coap');
let port             = 6666;
let server           = coap.createServer({ type: 'udp6' });
let mongoose         = require('mongoose');
let deviceDatabase   = mongoose.connect('mongodb://localhost/iot').connection;
let coapParser       = require('./coapParser');
let Device           = require('../models/device');

deviceDatabase.on('error', console.error.bind(console, 'connection error:'));
deviceDatabase.once('open', function() {
    console.log("successfully connected to the database");
});

/* it checks if there is a duplicate device, if there is then does not add */
let saveDevice = function(ip, advertisingString) {
    Device.findOne({ ipAddress: ip }, function(error, device){
        if (error) {
            throw error;
        } else {
            if (device == null) {
                let newDevice = coapParser.createDevice(ip, advertisingString);
                newDevice.save( function(error, device){
                    if(error) {
                        return console.error(error);
                    } else {
                        if (device != null) {
                            console.log("saving device successful");
                        }
                    }
                });
            } else {
                console.log("device already existed: " + device.ipAddress);
            }
        }
    });
};

server.on('request', function(req, res) {
    let deviceAddress = req.rsinfo.address;
    if (req.url == "/devices") {
        saveDevice(deviceAddress, req.payload.toString('ascii'));
    }
});

module.exports = {
    server: server,
    port: port,
    mongoose: mongoose,
    deviceDatabase: deviceDatabase,
};

/* test code */
let exampleAdvertisingString = "/sensor/temperature,1|/actuator/led,4|/sensor/fire,1";
let ip = "fe80::bc11:96ff:fedb:2717";
let ip2 = "fe90::bc11:96ff:fedb:2717";
saveDevice(ip, exampleAdvertisingString);
saveDevice(ip2, exampleAdvertisingString);



