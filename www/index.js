
var express = require('express');
var session = require('express-session');
var stylus = require('stylus');
var nib = require('nib');
var morgan = require('morgan');
var SQLiteStore = require('connect-sqlite3')(session);
var cookieParser = require('cookie-parser');
var bodyParser = require('body-parser');
var fs = require('fs');
var app = express();
var net = require('net');

var libclientserver = require("/usr/lib/node_modules/libclientserver.js");
var Cli = new libclientserver.Client("/tmp/CameraServer");
Cli.Connect();


function compile(str, path) {
	return stylus(str).set('style.css', path).use(nib());
}

app.set('views', __dirname + '/views');
app.set('view engine', 'jade');
app.use(morgan('dev'));
app.use(stylus.middleware(
	{
		src: __dirname + '/public',
		compile: compile
	}
));
app.use(express.static(__dirname + '/public'));
app.use(bodyParser.urlencoded({ extended: false }));
app.use(bodyParser.json());
app.use(cookieParser());

app.use(session({
	store: new SQLiteStore,
	secret: 'your secret',
	cookie: { maxAge: 7 * 24 * 60 * 60 * 1000 } // 1 week
}));

app.get('/', function (req, res) {
	if (req.session.IsUser == undefined)
	{
		res.redirect('/login');
	}
	else
	{
		res.redirect('/live');
	}
});

app.get('/login', function(req, res) {
	if (req.session.IsUser == true)
	{
		res.redirect('/live');
	}

	res.render('login',
		{
			title : 'Camera Login'
		}
	);
});

app.post('/login', function(req, res) {

	console.log(req.body);
	
	var args = {
		"action" : "UserAuth",
		"Username" : req.body["username"],
		"Password" : req.body["password"]
	};
	Cli.SendRequest(args, function(data, error) {
		if (error)
		{
			console.log("/login Error: " + error);
			res.render('login',
			{
				title : 'Camera Login',
				failed : true
			});
		}
		else
		{
			req.session.IsUser = true;
			res.redirect('/live');
		}
	});
});

app.get('/logout', function(req, res) {
	req.session.IsUser = false;
	req.session.destroy();
	res.redirect('/login');
	res.end();
});

app.get('/live', function(req, res)
{
	if (req.session.IsUser != true)
	{
		res.redirect('/login');
		return;
	}

	res.render('live',
		{
			title : 'Camera',
		}
	);
});

app.get('/videostream', function(req, res)
{
	if (req.session.IsUser != true)
	{
		res.status(401).send('Not logged in');
		return;
	}
	console.log("VideoStream: ", req.query);
	
	var args = {
		"action" : "WebStreamStart",
		"options" : {
			"vinput" : 0,
			"type" : "MKV_TRANS"
		}
	};
	
	Cli.SendRequest(args, function(data, error) {
		if (error)
		{
			console.log(error);
			res.status(500).send(error);
		}
		else
		{
			//console.log(data);
			
			var client = new net.Socket();
			client.connect(data["port"], "127.0.0.1");
			
			req.socket.on('close', function(data) {
				//console.log("Browser Socket Closed");
				client.destroy();
			});

			req.socket.on('drain', function(data) {
				//console.log("Broswer Socket writable again");
				client.resume();
			});

			client.on('data', function(data) {
				var status = res.write(data);
				//console.log(status);
				if (status == false)
				{
					//console.log("Pausing Socket");
					client.pause();
				}
			});
			
			client.on('close', function(data) {
				//console.log("No More Data");
			});
		}
	});
});

app.post('/api/generic', function(req, res)
{
	if (req.session.IsUser != true)
	{
		res.status(401).send('Not logged in');
		return;
	}

	Cli.SendRequest(req.body, function(data, error) {
		if (error)
		{
			res.status(500).send(error);
		}
		else
		{
			res.json(data);
		}
	});
});

app.get('/api/ping', function(req, res)
{
	if (req.session.IsUser != true)
	{
		res.status(401).send('Not logged in');
		return;
	}

	var args = { "action" : "PING" };
	Cli.SendRequest(args, function(data, error) {
		if (error)
		{
			res.status(500);
			res.send(error);
		}
		else
		{
			res.json(data);
		}
	});
});

app.get('/api/version', function(req, res)
{
	if (req.session.IsUser != true)
	{
		res.status(401).send('Not logged in');
		return;
	}

	var args = { "action" : "VERSION" };
	Cli.SendRequest(args, function(data, error) {
		if (error)
		{
			res.status(500).send(error);
		}
		else
		{
			res.json(data);
		}
	});
});


//Send a keepalive to backend so we do not get disconnected
function Ping()
{
	var args = { "action" : "PING" };
	Cli.SendRequest(args, function(data, error) {
		if (error)
		{
			console.log("Ping Error: " + error);
		}
		setTimeout(Ping, 60000);
	});
}
setTimeout(Ping, 60000);

if (process.env.PORT == undefined)
{
	console.log("PORT Is not set listening on port 3000 instead!!!");
	app.listen(3000);
}
else
{
	console.log("Listening on Port: " + process.env.PORT);
	app.listen(process.env.PORT);
}


