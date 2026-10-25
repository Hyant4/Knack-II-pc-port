.pragma library
// Default input mapping; presentation only. No device polling.
var families = {
  "ps": {
    "name": "PlayStation",
    "cross": "×",
    "circle": "○",
    "square": "□",
    "triangle": "△",
    "l1": "L1",
    "r1": "R1",
    "l2": "L2",
    "r2": "R2",
    "l3": "L3",
    "r3": "R3",
    "options": "Options",
    "back": "Share / Create",
    "touch": "Touchpad",
    "left": "Analógico esquerdo",
    "right": "Analógico direito"
  },
  "xbox": {
    "name": "Xbox",
    "cross": "A",
    "circle": "B",
    "square": "X",
    "triangle": "Y",
    "l1": "LB",
    "r1": "RB",
    "l2": "LT",
    "r2": "RT",
    "l3": "LS",
    "r3": "RS",
    "options": "Menu",
    "back": "View",
    "touch": "View",
    "left": "Analógico esquerdo",
    "right": "Analógico direito"
  },
  "nintendo": {
    "name": "Nintendo / Switch",
    "cross": "B",
    "circle": "A",
    "square": "Y",
    "triangle": "X",
    "l1": "L",
    "r1": "R",
    "l2": "ZL",
    "r2": "ZR",
    "l3": "L",
    "r3": "R",
    "options": "+",
    "back": "−",
    "touch": "−",
    "left": "Analógico esquerdo",
    "right": "Analógico direito"
  }
};
var commands = [
  {
    "id": "left-stick",
    "title": "Mover",
    "ps": "Analógico esquerdo",
    "label": "left",
    "keys": [
      "W",
      "A",
      "S",
      "D"
    ],
    "description": "Mova o analógico para andar na direção desejada."
  },
  {
    "id": "right-stick",
    "title": "Esquivar",
    "ps": "Analógico direito",
    "label": "right",
    "keys": [
      "↑",
      "↓",
      "←",
      "→"
    ],
    "description": "Toque o analógico direito na direção da esquiva. Neste jogo, ele não é um controle de câmera livre."
  },
  {
    "id": "square",
    "title": "Soco",
    "ps": "□",
    "label": "square",
    "keys": [
      "E"
    ],
    "description": "Pressione para atacar com socos. Golpes e combinações adicionais são apresentados ao longo do jogo."
  },
  {
    "id": "cross",
    "title": "Pular",
    "ps": "×",
    "label": "cross",
    "keys": [
      "Espaço"
    ],
    "description": "Pressione para saltar. Siga os tutoriais do jogo para saltos e movimentos adicionais."
  },
  {
    "id": "circle",
    "title": "Chute",
    "ps": "○",
    "label": "circle",
    "keys": [
      "Shift esquerdo"
    ],
    "description": "Pressione para atacar com um chute. O efeito pode mudar conforme a situação."
  },
  {
    "id": "triangle",
    "title": "Ação especial",
    "ps": "△",
    "label": "triangle",
    "keys": [
      "Q"
    ],
    "description": "Aciona a habilidade especial disponível. A função varia com o progresso, a transformação e as instruções do jogo."
  },
  {
    "id": "l1",
    "title": "Defender",
    "ps": "L1",
    "label": "l1",
    "keys": [
      "1"
    ],
    "description": "Segure para usar a defesa. Alguns ataques e situações exigem outra reação."
  },
  {
    "id": "r1",
    "title": "Alterar tamanho",
    "ps": "R1",
    "label": "r1",
    "keys": [
      "3"
    ],
    "description": "Alterna o tamanho de Knack quando o jogo permite, para atravessar passagens e resolver situações do cenário."
  },
  {
    "id": "l2",
    "title": "Especial da transformação",
    "ps": "L2",
    "label": "l2",
    "keys": [
      "R",
      "Mouse direito"
    ],
    "description": "Usa a ação especial de uma transformação que ofereça esse recurso. Não é um golpe universal de todas as formas."
  },
  {
    "id": "r2",
    "title": "Reunir no cooperativo",
    "ps": "R2",
    "label": "r2",
    "keys": [
      "F",
      "Mouse esquerdo"
    ],
    "description": "Aciona a função de deslocamento do cooperativo quando disponível. Siga a indicação do jogo para se reunir ao parceiro."
  },
  {
    "id": "options",
    "title": "Pausar",
    "ps": "Options",
    "label": "options",
    "keys": [
      "Enter"
    ],
    "description": "Abre o menu de pausa do próprio jogo. Os ajustes do port usam outro atalho: gatilho esquerdo + clique no analógico direito."
  },
  {
    "id": "touch",
    "title": "Tela de melhorias",
    "ps": "Touchpad",
    "label": "touch",
    "keys": [
      "Tab",
      "Backspace"
    ],
    "description": "O clique do touchpad abre a tela de melhorias. Em controles sem touchpad, View / Share / − emula esse clique. As teclas emulam lados diferentes da superfície."
  },
  {
    "id": "back",
    "title": "Clique no touchpad",
    "ps": "Touchpad direito",
    "label": "back",
    "keys": [
      "Backspace"
    ],
    "description": "Emula um clique no lado direito do touchpad. Para o lado esquerdo, mantenha o botão superior esquerdo junto. Gestos estão em Recursos do port."
  },
  {
    "id": "dpad",
    "title": "Direcional",
    "ps": "Direcional",
    "label": null,
    "keys": [
      "I",
      "J",
      "K",
      "L"
    ],
    "description": "Envia cima, esquerda, baixo e direita ao jogo. Use nos menus e quando solicitado pelas instruções na tela."
  },
  {
    "id": "l3",
    "title": "Clique no analógico esquerdo",
    "ps": "L3",
    "label": "l3",
    "keys": [
      "Z"
    ],
    "description": "Pressionar o analógico envia L3. Pressionar os dois analógicos juntos alterna o modo sensor herdado do runtime; deixe esse modo desligado no uso normal de Knack II."
  },
  {
    "id": "r3",
    "title": "Clique no analógico direito",
    "ps": "R3",
    "label": "r3",
    "keys": [
      "C",
      "Mouse do meio"
    ],
    "description": "Pressionar o analógico envia R3. Com o gatilho esquerdo pressionado, abre os ajustes do port."
  }
];
var photos = {
  "ps": {
    "file": "controller-ps-photo.png",
    "name": "DualSense · PlayStation",
    "width": 1200,
    "height": 675,
    "view": "200 75 800 550",
    "points": {
      "left-stick": [
        486,
        334,
        61
      ],
      "right-stick": [
        715,
        334,
        61
      ],
      "dpad": [
        375,
        233,
        65
      ],
      "square": [
        772,
        233,
        38
      ],
      "circle": [
        879,
        233,
        38
      ],
      "triangle": [
        825,
        181,
        38
      ],
      "cross": [
        826,
        287,
        38
      ],
      "back": [
        430,
        153,
        25
      ],
      "options": [
        770,
        153,
        25
      ],
      "touch": [
        600,
        181,
        100,
        55
      ],
      "l3": [
        486,
        334,
        22
      ],
      "r3": [
        715,
        334,
        22
      ]
    }
  },
  "xbox": {
    "file": "controller-xbox-one-frontal.png",
    "name": "Xbox Wireless Controller",
    "width": 1520,
    "height": 1041,
    "view": "-54 -22 1600 1100",
    "points": {
      "left-stick": [
        384,
        320,
        103
      ],
      "right-stick": [
        937,
        546,
        103
      ],
      "dpad": [
        558,
        552,
        105
      ],
      "square": [
        1022,
        320,
        62
      ],
      "circle": [
        1214,
        320,
        62
      ],
      "triangle": [
        1125,
        224,
        62
      ],
      "cross": [
        1125,
        420,
        62
      ],
      "back": [
        640,
        320,
        44
      ],
      "options": [
        850,
        320,
        44
      ],
      "l3": [
        384,
        320,
        38
      ],
      "r3": [
        937,
        546,
        38
      ]
    }
  },
  "nintendo": {
    "file": "controller-nintendo-front-clean.png",
    "name": "Nintendo Switch Pro Controller",
    "width": 581,
    "height": 568,
    "view": "47.5 92 485 334",
    "points": {
      "left-stick": [
        171,
        197,
        39
      ],
      "right-stick": [
        350,
        255,
        38
      ],
      "dpad": [
        225,
        255,
        38
      ],
      "square": [
        374,
        194,
        24
      ],
      "circle": [
        440,
        194,
        24
      ],
      "triangle": [
        407,
        165,
        24
      ],
      "cross": [
        407,
        224,
        24
      ],
      "back": [
        235,
        161,
        17
      ],
      "options": [
        345,
        161,
        17
      ],
      "l3": [
        171,
        197,
        15
      ],
      "r3": [
        350,
        255,
        15
      ]
    }
  }
};

