from flask import Flask, request, render_template
app = Flask(__name__, static_folder='.', static_url_path='')

status = ['Lock', 'Unlock', 'None']
global current_status
current_status = 2

@app.route('/admin', methods=['GET','POST'])
def post():
    global current_status
    if request.method == 'POST':
        state_no = request.form.get("state")
        if state_no == None:
            state_no = 2
        current_status = state_no
    return render_template('admin.html', message = 'Current Request Status: ' + status[int(current_status)])

@app.route('/get-status', methods=['GET'])
def get_status():
    global current_status
    return str(current_status)


app.run(host='192.168.0.11', port=8000, debug=False)