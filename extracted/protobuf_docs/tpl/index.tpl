{{$header.tpl}}

<table width="100%">
  <tr>
    <th colspan="2">
      <h3 style="margin-top: 4px">Packages</h3>
    </th>
  </tr>
  {{#Site_package}}
    <tr>
      <td>
        <div><code><a href="#{{Package_anchor}}">{{Package_name}}</a></code></div>
        <div style="font-style: italic; margin-top: 4px; margin-left: 16px;">
          {{Package_brief}}
        </div>
      </td>
    </tr>
  {{/Site_package}}
</table>

{{#Site_package}}
  <h2 id="{{Package_anchor}}">{{Package_name}}</h2>
  <p>{{Package_brief}}</p>
  {{Package_details}}
  <table width="100%">
    <tr>
      <th colspan="2">
        <h3 style="margin-top: 4px">Files</h3>
      </th>
    </tr>
    {{#Package_file}}
      <tr>
        <td>
          <div><code><a href="{{File_url}}">{{File_full_path}}</a></code></div>
          <div style="font-style: italic; margin-top: 4px; margin-left: 16px;">
            {{File_brief}}
          </div>
        </td>
      </tr>
    {{/Package_file}}
  </table>
{{/Site_package}}
{{$footer.tpl}}
