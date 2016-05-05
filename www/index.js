
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
	res.render('login',
		{
			title : 'Camera Login'
		}
	);
});

app.post('/login', function(req, res) {
	res.render('login',
		{
			title : 'Camera Login'
		}
	);
});


app.get('/logout', function(req, res) {
	req.session.destroy();
	res.redirect('/login');
});

app.listen(3000);

