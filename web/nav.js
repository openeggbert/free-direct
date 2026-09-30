// Highlight the active nav link based on the current page filename.
(function () {
  var path = window.location.pathname.split('/').pop() || 'index.html';
  var links = document.querySelectorAll('.sidebar nav a');
  links.forEach(function (a) {
    var href = a.getAttribute('href').split('/').pop();
    if (href === path) a.classList.add('active');
  });
})();
