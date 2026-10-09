import QtQuick

Item {
    id: icon
    property string name: "shelf"
    property color color: "white"
    implicitWidth: 20
    implicitHeight: 20
    onNameChanged: canvas.requestPaint()
    onColorChanged: canvas.requestPaint()
    Canvas {
        id: canvas
        anchors.fill: parent
        onPaint: {
            const c = getContext("2d"); c.reset(); c.scale(width / 24, height / 24);
            c.strokeStyle = icon.color; c.fillStyle = icon.color; c.lineWidth = 1.7;
            c.lineCap = "round"; c.lineJoin = "round";
            function line(points) { c.beginPath(); c.moveTo(points[0],points[1]); for(let i=2;i<points.length;i+=2)c.lineTo(points[i],points[i+1]); c.stroke(); }
            function box(x,y,w,h,r) { c.beginPath();c.roundedRect(x,y,w,h,r,r);c.stroke(); }
            function circle(x,y,r) { c.beginPath();c.arc(x,y,r,0,Math.PI*2);c.stroke(); }
            switch(icon.name) {
            case "shelf": line([4,5,4,17,7,20,17,20,20,17,20,5]);line([4,14,9,14,10,16,14,16,15,14,20,14]);line([8,4,16,4]);line([8,8,16,8]);break;
            case "chevron": line([9,6,15,12,9,18]);break;
            case "down": line([7,9,12,14,17,9]);break;
            case "close": line([7,7,17,17]);line([17,7,7,17]);break;
            case "minus": line([6,12,18,12]);break;
            case "plus": line([12,5,12,19]);line([5,12,19,12]);break;
            case "search": circle(10.5,10.5,6);line([15,15,20,20]);break;
            case "more": circle(5,12,0.8);circle(12,12,0.8);circle(19,12,0.8);break;
            case "settings": circle(12,12,3.1);circle(12,12,7);for(let i=0;i<8;i++){let a=i*Math.PI/4;line([12+Math.cos(a)*7,12+Math.sin(a)*7,12+Math.cos(a)*9,12+Math.sin(a)*9]);}break;
            case "folder": c.beginPath();c.moveTo(3,7);c.lineTo(3,18);c.quadraticCurveTo(3,20,5,20);c.lineTo(19,20);c.quadraticCurveTo(21,20,21,18);c.lineTo(21,9);c.quadraticCurveTo(21,7,19,7);c.lineTo(11,7);c.lineTo(9,4);c.lineTo(5,4);c.quadraticCurveTo(3,4,3,7);c.stroke();break;
            case "image": box(3,3,18,18,3);circle(8,8,1.5);line([4,17,9,12,13,16,16,11,20,16]);break;
            case "file": line([14,3,5,3,5,21,19,21,19,8,14,3,14,8,19,8]);line([9,13,15,13]);line([9,17,15,17]);break;
            case "text": line([4,5,20,5]);line([12,5,12,20]);line([8,20,16,20]);break;
            case "url": c.save();c.translate(12,12);c.rotate(-Math.PI/4);box(-9,-4,11,8,4);box(-2,-4,11,8,4);line([-4,0,4,0]);c.restore();break;
            case "copy": box(8,8,12,13,2);line([15,4,5,4,4,5,4,15]);break;
            case "paste": box(5,6,14,15,2);box(8,3,8,5,2);break;
            case "trash": line([4,6,20,6]);line([9,3,15,3]);line([6,6,7,21,17,21,18,6]);line([10,10,10,17]);line([14,10,14,17]);break;
            case "pin": line([8,3,16,3,15,10,18,14,6,14,9,10,8,3]);line([12,14,12,22]);break;
            case "undo": line([4,5,4,11,10,11]);c.beginPath();c.arc(12,13,8,Math.PI*1.1,Math.PI*2.3);c.stroke();break;
            case "check": line([5,12,10,17,20,6]);break;
            case "warning": line([12,3,22,21,2,21,12,3]);line([12,9,12,14]);circle(12,18,0.3);break;
            case "external": line([14,3,21,3,21,10]);line([21,3,11,13]);line([10,5,4,5,4,20,19,20,19,14]);break;
            case "arrowUp": line([12,20,12,4]);line([6,10,12,4,18,10]);break;
            case "arrowDown": line([12,4,12,20]);line([6,14,12,20,18,14]);break;
            case "link": circle(6,6,3);circle(18,18,3);line([8,8,16,16]);break;
            case "sun": circle(12,12,4);for(let i=0;i<8;i++){let a=i*Math.PI/4;line([12+Math.cos(a)*7,12+Math.sin(a)*7,12+Math.cos(a)*9,12+Math.sin(a)*9]);}break;
            case "moon": c.beginPath();c.arc(12,12,9,0.1,Math.PI*1.5);c.quadraticCurveTo(6,16,21,13);c.stroke();break;
            case "monitor": box(3,4,18,13,2);line([12,17,12,21]);line([8,21,16,21]);break;
            case "keyboard": box(2,5,20,14,3);for(let y=9;y<14;y+=4)for(let x=6;x<20;x+=4)line([x,y,x+.4,y]);line([7,16,17,16]);break;
            case "grip": for(let y=7;y<=17;y+=5){circle(9,y,0.6);circle(15,y,0.6);}break;
            case "video": box(3,6,13,13,2);line([16,10,21,7,21,18,16,15]);break;
            case "audio": line([9,17,9,5,20,3,20,15]);circle(6,18,3);circle(17,16,3);break;
            default: box(4,4,16,16,4);break;
            }
        }
    }
}
