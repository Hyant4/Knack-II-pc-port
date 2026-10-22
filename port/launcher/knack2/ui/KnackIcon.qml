import QtQuick

Canvas {
    id: icon
    // Local vector glyphs; no icon fonts, emoji, downloads or game assets.
    property string symbol: "relic"
    property color ink: "#e9c27b"
    implicitWidth: 20
    implicitHeight: 20
    Accessible.ignored: true
    onSymbolChanged: requestPaint()
    onInkChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    onPaint: {
        const c = getContext("2d");
        c.reset(); c.scale(width / 24, height / 24);
        c.strokeStyle = ink; c.fillStyle = ink;
        c.lineWidth = 1.65; c.lineCap = "round"; c.lineJoin = "round";
        function path(p, close, fill) {
            c.beginPath(); c.moveTo(p[0][0], p[0][1]);
            for (let i = 1; i < p.length; ++i) c.lineTo(p[i][0], p[i][1]);
            if (close) c.closePath();
            if (fill) c.fill(); else c.stroke();
        }
        function circle(x, y, r) { c.beginPath(); c.arc(x,y,r,0,Math.PI*2); c.stroke(); }
        switch (symbol) {
        case "language":
            circle(12,12,9);
            path([[3,12],[21,12]],false,false);
            c.beginPath(); c.moveTo(12,3);
            c.bezierCurveTo(4,10,4,14,12,21);
            c.bezierCurveTo(20,14,20,10,12,3); c.stroke();
            break;
        case "home":
            path([[3,11],[12,3],[21,11]], false, false);
            path([[5,10],[5,21],[10,21],[10,15],[14,15],[14,21],[19,21],[19,10]], false, false);
            break;
        case "settings":
            circle(12,12,6); circle(12,12,2.2);
            for (let i=0;i<8;++i) {
                const a=i*Math.PI/4;
                path([[12+8*Math.cos(a),12+8*Math.sin(a)], [12+10*Math.cos(a),12+10*Math.sin(a)]],false,false);
            }
            break;
        case "logs":
            path([[5,3],[15,3],[20,8],[20,21],[5,21]],true,false);
            path([[15,3],[15,8],[20,8]],false,false);
            path([[8,12],[16,12]],false,false); path([[8,16],[16,16]],false,false);
            break;
        case "folder":
            path([[3,8],[3,5],[9,5],[12,8],[21,8],[19,20],[3,20],[3,8],[21,8]],false,false);
            break;
        case "check":
            path([[12,2],[21,6],[20,15],[12,22],[4,15],[3,6]],true,false);
            path([[7,12],[10,15],[17,8]],false,false);
            break;
        case "play": path([[7,3],[21,12],[7,21]],true,true); break;
        case "stop": c.strokeRect(5,5,14,14); c.fillRect(8,8,8,8); break;
        case "players":
            circle(9,7,3); circle(17,8,2.5);
            path([[2,21],[3,16],[6,13],[12,13],[15,16],[16,21]],false,false);
            path([[16,14],[20,15],[22,21]],false,false);
            break;
        case "save":
            path([[4,3],[17,3],[21,7],[21,21],[3,21],[3,3]],true,false);
            path([[7,3],[7,9],[16,9],[16,3]],false,false);
            c.strokeRect(7,14,10,7); break;
        case "copy":
            path([[8,7],[8,3],[21,3],[21,17],[17,17]],false,false);
            c.strokeRect(3,7,13,14); break;
        case "alert":
            path([[12,3],[22,21],[2,21]],true,false);
            path([[12,9],[12,14]],false,false); c.fillRect(11,17,2,2); break;
        case "gamepad":
            path([[7,7],[17,7],[20,10],[22,18],[20,20],[16,16],[8,16],[4,20],[2,18],[4,10]],true,false);
            path([[5,11],[9,11]],false,false); path([[7,9],[7,13]],false,false);
            circle(16,11,.8); circle(19,13,.8); break;
        case "monitor":
            c.strokeRect(2,3,20,14); path([[12,17],[12,22]],false,false);
            path([[7,22],[17,22]],false,false); break;
        default:
            path([[12,2],[21,10],[16,21],[7,21],[3,10]],true,false);
            path([[12,2],[8,10],[12,21],[16,10],[12,2]],false,false);
            path([[3,10],[21,10]],false,false); break;
        }
    }
}
