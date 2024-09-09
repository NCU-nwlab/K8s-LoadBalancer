const express = require('express');
const app = express();
const port = 3000;

function calculate() {
	var sign = 1;
	var deno = 1;
	var sum = 0;
	var t = sign * 1 / deno
	while(Math.abs(t)>10e-9){
		sum += t;
		sign = sign * -1;
		deno += 2;
		t = sign *1 /deno;
	}
	var pi = sum * 4;
	return pi;
}

app.get('/', (req, res) => {
	res.send(process.env.NODE_IP + " Result: " + calculate());
})

app.listen(port, () => {
	console.log('Example app listening at http://localhost:'+ port)
})
