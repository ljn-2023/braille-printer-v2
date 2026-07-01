from flask import Flask, request, render_template_string, redirect, url_for
from pypinyin import pinyin, Style
import requests
import threading
import webbrowser

app = Flask(__name__)

# =========================
# ESP32
# =========================
ESP32_BASE = "http://172.20.10.8"

PRINT_URL = ESP32_BASE + "/print"
FINISH_URL = ESP32_BASE + "/finish"

# =========================
# 声母
# =========================
INITIALS = {
    "b":"12",
    "p":"1234",
    "m":"134",
    "f":"124",

    "d":"145",
    "t":"234",
    "n":"1345",
    "l":"123",

    "g":"13",
    "k":"14",
    "h":"125",

    "j":"24",
    "q":"245",
    "x":"1346",

    "zh":"156",
    "ch":"16",
    "sh":"146",

    "r":"1245",

    "z":"135",
    "c":"1235",
    "s":"2345",

    "y":"56",
    "w":"46"
}

# =========================
# 韵母
# =========================
FINALS = {

    "a":"1",
    "o":"13",
    "e":"15",

    "i":"2",
    "u":"3",
    "v":"23",

    "ai":"14",
    "ei":"25",
    "ao":"24",
    "ou":"34",

    "an":"124",
    "en":"125",

    "ang":"145",
    "eng":"245",
    "ong":"346",

    "ia":"126",
    "ie":"146",
    "iao":"246",
    "iu":"235",

    "ian":"1236",
    "iang":"13456",
    "iong":"12456",

    "ua":"136",
    "uo":"356",

    "uai":"12346",
    "uan":"2346",
    "uang":"23456",

    "ui":"236",
    "un":"2356",

    "in":"1235",
    "ing":"1345",

    "er":"256"
}

# =========================
# 声调
# =========================
TONES = {
    "1":"1",
    "2":"12",
    "3":"14",
    "4":"145",
    "5":""
}


def split_pinyin(py):

    for ini in sorted(
        INITIALS.keys(),
        key=len,
        reverse=True
    ):

        if py.startswith(ini):

            return ini, py[len(ini):]

    return "", py


def pinyin_to_braille(py):

    tone = "5"

    if py[-1].isdigit():

        tone = py[-1]

        py = py[:-1]

    initial, final = split_pinyin(py)

    result = []

    if initial:

        result.append(
            INITIALS[initial]
        )

    if final:

        code = FINALS.get(
            final,
            "000"
        )

        if tone != "5":

            code += "-" + TONES[tone]

        result.append(code)

    return result


def chinese_to_code(text):

    pys = pinyin(
        text,
        style=Style.TONE3,
        neutral_tone_with_five=True
    )

    result = []

    for item in pys:

        result.extend(
            pinyin_to_braille(
                item[0]
            )
        )

    return " ".join(result)


HTML = """

<html>

<head>

<title>盲文打印机</title>

<style>

body{

font-family:Arial;

text-align:center;

margin-top:60px;

}

input{

width:700px;

height:70px;

font-size:30px;

}

button{

width:220px;

height:70px;

font-size:25px;

margin:10px;

}

</style>

</head>

<body>

<h1>盲文打印机</h1>

<form method="post">

<input
name="text"
required>

<br><br>

<button>
打印
</button>

</form>

<form
method="post"
action="/finish">

<button>

停止走纸

</button>

</form>

<div style="margin:20px 0;">

<form method="post" action="/move_x" style="display:inline-block;">

<input type="hidden" name="direction" value="left">

<button type="submit" style="background-color:#ff9800;">X 左移</button>

</form>

<form method="post" action="/move_x" style="display:inline-block;">

<input type="hidden" name="direction" value="right">

<button type="submit" style="background-color:#4caf50;">X 右移</button>

</form>

</div>

<p>

{{result}}

</p>

<p>

{{code}}

</p>

</body>

</html>

"""


@app.route(
"/",
methods=[
"GET",
"POST"
]
)
def index():

    result = "等待输入"

    code = ""

    if request.method == "POST":

        try:

            text = request.form["text"]

            code = chinese_to_code(
                text
            )

            print(text)

            print(code)

            requests.get(

                PRINT_URL,

                params={

                    "code": code

                },

                timeout=10

            )

            result = "打印中"

        except Exception as e:

            result = str(e)

    return render_template_string(

        HTML,

        result=result,

        code=code

    )


@app.route(
"/finish",
methods=["POST"]
)
def finish():

    try:

        requests.get(
            FINISH_URL
        )

    except:

        pass

    return redirect("/")


@app.route("/move_x", methods=["POST"])
def handle_move_x():
    direction = request.form.get("direction", "")
    mm = 1.0 if direction == "right" else -1.0

    try:
        requests.get(ESP32_BASE + "/movex", params={"mm": mm}, timeout=10)
        result = f"X轴 {'右移' if direction == 'right' else '左移'} 1mm"
    except Exception as e:
        result = str(e)

    return render_template_string(HTML, result=result, code="")


def open_browser():

    webbrowser.open(

        "http://127.0.0.1:5000"

    )


if __name__ == "__main__":

    threading.Timer(
        2,
        open_browser
    ).start()

    app.run(

        host="0.0.0.0",

        port=5000

    )